cmake_minimum_required(VERSION 3.20)

function(spirv2hpp)
    set(oneValueArgs SOURCE_FILE HEADER_FILE HEADER_NAMESPACE)
    cmake_parse_arguments(spirv2hpp "" "${oneValueArgs}" "" ${ARGN})

    file(READ ${spirv2hpp_SOURCE_FILE} spirv)
    
    set(file_content "#pragma once\n\n")
    set(file_content "${file_content}#include <cstdint>\n\n")
    set(file_content "${file_content}namespace ${spirv2hpp_HEADER_NAMESPACE} {\n")
    set(file_content "${file_content}static constexpr std::uint32_t code[] = {${spirv}}\\;\n")
    set(file_content "${file_content}static constexpr std::uint32_t size = sizeof(code)\\;\n")
    set(file_content "${file_content}} //namespace ${spirv2hpp_HEADER_NAMESPACE}\n")

    file(WRITE ${spirv2hpp_HEADER_FILE} ${file_content})

endfunction()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "No input file path provided via 'INPUT_FILE'.")
endif()

if(NOT DEFINED HEADER_FILE)
    message(FATAL_ERROR "No output file path provided via 'HEADER_FILE'.")
endif()

if(NOT DEFINED HEADER_NAMESPACE)
    message(FATAL_ERROR "No header namespace provided via 'HEADER_NAMESPACE'.")
endif()

spirv2hpp(
    SOURCE_FILE ${INPUT_FILE}
    HEADER_FILE ${HEADER_FILE}
    HEADER_NAMESPACE ${HEADER_NAMESPACE}
)
