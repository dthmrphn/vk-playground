add_library(layer MODULE "layer.cpp")
target_link_libraries(layer PRIVATE Vulkan::Vulkan fmt::fmt imguilib)

set(layer_path "${CMAKE_CURRENT_BINARY_DIR}/liblayer.so")
configure_file("layer.json" "${CMAKE_CURRENT_BINARY_DIR}/layer.json" @ONLY)
