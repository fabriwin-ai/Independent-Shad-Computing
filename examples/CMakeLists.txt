add_executable(isc_run isc_run/main.cpp)
target_link_libraries(isc_run PRIVATE isc::core)
if(MSVC)
    target_compile_options(isc_run PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(isc_run PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Copy the .fa samples next to the binary for convenience.
file(GLOB ISC_SAMPLE_PIPELINES ${CMAKE_CURRENT_SOURCE_DIR}/*.fa)
foreach(fa ${ISC_SAMPLE_PIPELINES})
    configure_file(${fa} ${CMAKE_CURRENT_BINARY_DIR}/ COPYONLY)
endforeach()
