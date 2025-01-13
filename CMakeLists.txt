cmake_minimum_required(VERSION 3.15)

project(vk-playground C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(spirv_library)

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
find_package(glfw3 REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)

set(libraries common glfw glm::glm fmt::fmt Vulkan::Vulkan)

add_subdirectory(common)
add_subdirectory(examples/triangle)
add_subdirectory(examples/texture)
add_subdirectory(examples/compute)
