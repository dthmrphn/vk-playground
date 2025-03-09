add_library(imguilib STATIC 
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_impl_vulkan.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
target_include_directories(imguilib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET imguilib PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(stblib INTERFACE)
target_include_directories(stblib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
