add_executable(isc_tests
    test_main.cpp
    test_lexer.cpp
    test_parser.cpp
    test_ir.cpp
    test_profile.cpp
    test_cache.cpp
    test_scheduler.cpp
    test_optimizer.cpp
    test_plan_lod.cpp
    test_cpu_backend.cpp
    test_vulkan.cpp
)
target_link_libraries(isc_tests PRIVATE isc::core)
target_include_directories(isc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(isc_tests PRIVATE /W4 /permissive- /utf-8 /MP)
    target_compile_definitions(isc_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# One CTest entry per area so failures are easy to locate.
foreach(area lexer parser ir profile cache scheduler optimizer plan lod cpu vulkan)
    add_test(NAME isc.${area} COMMAND isc_tests ${area})
endforeach()
