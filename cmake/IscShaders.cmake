# Compiles GLSL compute shaders to SPIR-V with glslc and embeds the bytecode
# into the target as C++ byte arrays, so the layer ships as a single library
# with no loose .spv files to locate at runtime.
#
# Generated header per shader: <OUTPUT_DIR>/<name>.spv.h exposing
#     static const unsigned char isc_spv_<name>[];
#     static const unsigned long long isc_spv_<name>_size;

function(isc_add_spirv_shaders target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SHADERS" ${ARGN})
    file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR})

    set(generated_headers "")
    foreach(shader ${ARG_SHADERS})
        get_filename_component(name ${shader} NAME_WE)
        set(src  ${CMAKE_CURRENT_SOURCE_DIR}/${shader})
        set(spv  ${ARG_OUTPUT_DIR}/${name}.spv)
        set(hdr  ${ARG_OUTPUT_DIR}/${name}.spv.h)

        add_custom_command(
            OUTPUT  ${spv}
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 -O -o ${spv} ${src}
            DEPENDS ${src}
            COMMENT "glslc ${shader}"
            VERBATIM)

        add_custom_command(
            OUTPUT  ${hdr}
            COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${hdr} -DSYMBOL=isc_spv_${name}
                    -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
            DEPENDS ${spv} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
            COMMENT "embed ${name}.spv"
            VERBATIM)

        list(APPEND generated_headers ${hdr})
    endforeach()

    add_custom_target(${target}_spirv DEPENDS ${generated_headers})
    add_dependencies(${target} ${target}_spirv)
    target_include_directories(${target} PRIVATE ${ARG_OUTPUT_DIR})
endfunction()
