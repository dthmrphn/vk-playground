add_spirv_library(layer_shaders GLSL "layer.vert" "layer.frag")

add_library(layer MODULE "layer.cpp")
target_link_libraries(layer PRIVATE Vulkan::Vulkan fmt::fmt imguilib layer_shaders)

set(layer_path "${CMAKE_CURRENT_BINARY_DIR}/liblayer.so")
configure_file("layer.json" "${CMAKE_CURRENT_BINARY_DIR}/layer.json" @ONLY)
