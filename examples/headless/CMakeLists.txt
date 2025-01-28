add_spirv_library(headless_shaders GLSL "headless.comp")
add_executable(headless "headless.cpp")
target_link_libraries(headless PRIVATE ${libraries} headless_shaders)
