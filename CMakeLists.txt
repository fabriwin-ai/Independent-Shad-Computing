cmake_minimum_required(VERSION 3.24)

project(IndependentShadComputing
    VERSION 0.1.0
    DESCRIPTION "Engine-independent graphics enhancement layer"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ISC_BUILD_TESTS          "Build the test suite"                              ON)
option(ISC_BUILD_EXAMPLES       "Build the example runner (isc_run)"                ON)
option(ISC_WITH_ENKITS          "Fetch enkiTS (pinned) and use it as the scheduler" ON)
option(ISC_WITH_SIMPLE_ECS      "Fetch simple-ecs (pinned) for the ECS layer"       ON)
option(ISC_WITH_VULKAN          "Build the Vulkan compute backend"                  ON)
option(ISC_WARNINGS_AS_ERRORS   "Treat compiler warnings as errors"                 OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(IscDependencies)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(isc_core STATIC
    src/core/types.cpp
    src/core/render_target.cpp
    src/core/device_caps.cpp
    src/core/profile.cpp
    src/lang/diagnostics.cpp
    src/lang/lexer.cpp
    src/lang/parser.cpp
    src/lang/ast_dump.cpp
    src/ir/lower.cpp
    src/ir/ir_dump.cpp
    src/sched/task_graph.cpp
    src/sched/serial_scheduler.cpp
    src/sched/enkits_scheduler.cpp
    src/opt/optimizer.cpp
    src/plan/execution_plan.cpp
    src/lod/adaptive_controller.cpp
    src/backend/cpu/cpu_backend.cpp
    src/util/image_io.cpp
    src/runtime.cpp
)
add_library(isc::core ALIAS isc_core)

target_include_directories(isc_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(isc_core PUBLIC
    ISC_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(isc_core PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(isc_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    if(ISC_WARNINGS_AS_ERRORS)
        target_compile_options(isc_core PRIVATE /WX)
    endif()
else()
    target_compile_options(isc_core PRIVATE -Wall -Wextra -Wpedantic)
    if(ISC_WARNINGS_AS_ERRORS)
        target_compile_options(isc_core PRIVATE -Werror)
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(isc_core PUBLIC Threads::Threads)

# --- enkiTS scheduler -------------------------------------------------------
if(ISC_HAS_ENKITS)
    target_link_libraries(isc_core PRIVATE enkiTS)
    target_compile_definitions(isc_core PUBLIC ISC_HAS_ENKITS=1)
endif()

# --- simple-ecs (Phase 4: wired, not used by code yet) ----------------------
if(ISC_HAS_SIMPLE_ECS)
    target_link_libraries(isc_core PRIVATE isc::simple_ecs)
    target_compile_definitions(isc_core PUBLIC ISC_HAS_SIMPLE_ECS=1)
endif()

# --- Vulkan backend ----------------------------------------------------------
set(ISC_HAS_VULKAN OFF)
if(ISC_WITH_VULKAN)
    find_package(Vulkan COMPONENTS glslc)
    if(Vulkan_FOUND AND Vulkan_glslc_FOUND)
        set(ISC_HAS_VULKAN ON)
    else()
        message(WARNING "ISC: Vulkan SDK or glslc not found - building without the Vulkan backend. "
                        "Install the Vulkan SDK (VULKAN_SDK env var) or pass -DISC_WITH_VULKAN=OFF.")
    endif()
endif()

if(ISC_HAS_VULKAN)
    include(IscShaders)
    isc_add_spirv_shaders(isc_core
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders
        SHADERS
            shaders/resample.comp
            shaders/blur_h.comp
            shaders/detail.comp
            shaders/reduce.comp
            shaders/optimize.comp
            shaders/composite.comp)
    target_sources(isc_core PRIVATE
        src/backend/vulkan/vk_context.cpp
        src/backend/vulkan/vk_allocator.cpp
        src/backend/vulkan/vk_backend.cpp)
    target_link_libraries(isc_core PRIVATE Vulkan::Vulkan)
    target_compile_definitions(isc_core PUBLIC ISC_HAS_VULKAN=1)
else()
    target_sources(isc_core PRIVATE src/backend/vulkan/vk_stub.cpp)
endif()

message(STATUS "ISC: enkiTS=${ISC_HAS_ENKITS} simple-ecs=${ISC_HAS_SIMPLE_ECS} vulkan=${ISC_HAS_VULKAN}")

# ---------------------------------------------------------------------------
# Examples and tests
# ---------------------------------------------------------------------------
if(ISC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ISC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
