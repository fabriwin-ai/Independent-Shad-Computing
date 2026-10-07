# Dependency policy (see DEPENDENCIES.md):
#   * Third-party code is FETCHED at configure time, never copied into the repo.
#   * Every dependency is pinned to an immutable commit hash.
#   * Offline / air-gapped builds: point FETCHCONTENT_SOURCE_DIR_<NAME> at a local
#     checkout of the same revision, or disable the dependency (ISC_WITH_*=OFF).
#     Without enkiTS the runtime falls back to the built-in serial scheduler.

include(FetchContent)

set(ISC_ENKITS_REPOSITORY     "https://github.com/dougbinks/enkiTS.git")
set(ISC_ENKITS_REVISION       "404a3bf8f855039dfff2052184d6308655286c07")
set(ISC_SIMPLE_ECS_REPOSITORY "https://github.com/lchsk/simple-ecs.git")
set(ISC_SIMPLE_ECS_REVISION   "1e887ecc28f36e41604fc21a4bf311a0e778892a")

set(ISC_HAS_ENKITS OFF)
set(ISC_HAS_SIMPLE_ECS OFF)

if(ISC_WITH_ENKITS)
    set(ENKITS_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
    set(ENKITS_INSTALL           OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(enkits
        GIT_REPOSITORY ${ISC_ENKITS_REPOSITORY}
        GIT_TAG        ${ISC_ENKITS_REVISION}
        GIT_PROGRESS   ON)
    FetchContent_MakeAvailable(enkits)

    if(TARGET enkiTS)
        set(ISC_HAS_ENKITS ON)
    else()
        message(FATAL_ERROR "ISC: enkiTS was fetched but did not define the 'enkiTS' target.")
    endif()
endif()

if(ISC_WITH_SIMPLE_ECS)
    # simple-ecs is header-only. SOURCE_SUBDIR points at a directory without a
    # CMakeLists.txt so the sources are populated but not added to the build.
    FetchContent_Declare(simple_ecs
        GIT_REPOSITORY ${ISC_SIMPLE_ECS_REPOSITORY}
        GIT_TAG        ${ISC_SIMPLE_ECS_REVISION}
        GIT_PROGRESS   ON
        SOURCE_SUBDIR  isc-header-only-no-cmake)
    FetchContent_MakeAvailable(simple_ecs)

    add_library(isc_simple_ecs INTERFACE)
    add_library(isc::simple_ecs ALIAS isc_simple_ecs)
    target_include_directories(isc_simple_ecs SYSTEM INTERFACE "${simple_ecs_SOURCE_DIR}/include")
    set(ISC_HAS_SIMPLE_ECS ON)
endif()
