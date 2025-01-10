add_spirv_library(texture_shaders GLSL "texture.vert" "texture.frag")
add_executable(texture "texture.cpp")
target_link_libraries(texture PRIVATE ${libraries} texture_shaders)
