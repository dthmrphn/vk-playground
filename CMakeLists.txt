cmake_minimum_required(VERSION 3.15)

project(vk-playground C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(spirv_library)

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)

set(WSI "glfw" CACHE STRING "use wsi backend (wayland, xcb, glfw)")

set(libraries common glm::glm fmt::fmt Vulkan::Vulkan stblib)

add_subdirectory(3rdparty)
add_subdirectory(wsi)
add_subdirectory(common)
add_subdirectory(examples/triangle)
add_subdirectory(examples/texture)
add_subdirectory(examples/compute)
add_subdirectory(examples/headless)
add_subdirectory(examples/device)
add_subdirectory(examples/layer)
