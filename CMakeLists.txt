cmake_minimum_required(VERSION 3.15)

project(vk-playground C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/spirv_library.cmake)

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
find_package(glfw3 REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)

add_subdirectory(common)

set(libraries common glfw glm fmt::fmt Vulkan::Vulkan)

add_spirv_library(triangle_shaders GLSL "shaders/triangle.vert" "shaders/triangle.frag")
add_executable(triangle "examples/triangle.cpp")
target_link_libraries(triangle PRIVATE ${libraries} triangle_shaders)

add_spirv_library(texture_shaders GLSL "shaders/texture.vert" "shaders/texture.frag")
add_executable(texture "examples/texture.cpp")
target_link_libraries(texture PRIVATE ${libraries} texture_shaders)

