add_spirv_library(compute_shaders GLSL "compute.vert" "compute.frag" "compute.comp")
add_executable(compute "compute.cpp")
target_link_libraries(compute PRIVATE ${libraries} compute_shaders)
