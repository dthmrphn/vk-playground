find_package(PkgConfig)
pkg_check_modules(WAYLAND wayland-client wayland-scanner)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(WAYLAND DEFAULT_MSG WAYLAND_LIBRARIES)

set(wayland_protocols_path "/usr/share/wayland-protocols/stable")

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/xdg-shell.h
    COMMAND wayland-scanner client-header ${wayland_protocols_path}/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell.h
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/xdg-shell.c
    COMMAND wayland-scanner private-code ${wayland_protocols_path}/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell.c
)

add_library(wsi STATIC wsi.wayland.cpp ${CMAKE_BINARY_DIR}/xdg-shell.c ${CMAKE_BINARY_DIR}/xdg-shell.h)
target_include_directories(wsi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR})

