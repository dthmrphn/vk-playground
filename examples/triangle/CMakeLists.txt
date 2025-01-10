add_spirv_library(triangle_shaders GLSL "triangle.vert" "triangle.frag")
add_executable(triangle "triangle.cpp")
target_link_libraries(triangle PRIVATE ${libraries} triangle_shaders)
