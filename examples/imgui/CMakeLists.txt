add_spirv_library(imgui_shaders GLSL "imgui.vert" "imgui.frag")
add_executable(imgui "imgui.cpp")
target_link_libraries(imgui PRIVATE ${libraries} imgui_shaders imguilib)
