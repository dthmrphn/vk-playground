function(glsl2spirv glsl header namespace)
    get_filename_component(shader_name ${glsl} NAME)
    set(spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    set(spirv_full "${spirv_dir}/${shader_name}.spv")

    file(MAKE_DIRECTORY ${spirv_dir})

    add_custom_command(
        OUTPUT ${spirv_full}
        COMMAND glslangValidator ARGS
            ${glsl} 
            "-V"
            "-x"
            "-o" ${spirv_full}
        DEPENDS ${glsl}
        COMMENT "compiling ${glsl}"
    )

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} ARGS
            "-DINPUT_FILE=${spirv_full}"
            "-DHEADER_FILE=${header}"
            "-DHEADER_NAMESPACE=${namespace}"
            "-P ${CMAKE_SOURCE_DIR}/cmake/spirv2hpp.cmake"
        DEPENDS ${spirv_full}
        COMMENT "generating header ${header}"
    )
endfunction()

function(add_spirv_library name)
    cmake_parse_arguments(add_spirv_library "" "" "GLSL" ${ARGN})
    set(header_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}")

    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${header_dir})

    foreach(source ${add_spirv_library_GLSL})
        get_filename_component(file_name ${source} NAME)
        set(source_full "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        set(header_full "${header_dir}/${file_name}.hpp")
        string(REGEX REPLACE "\\." "_" namespace ${file_name})

        glsl2spirv(${source_full} ${header_full} ${namespace})

        target_sources(${name} INTERFACE ${header_full})
    endforeach()

endfunction()
