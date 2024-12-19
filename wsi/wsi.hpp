#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace wsi {

namespace detail {
struct non_copyable {
    non_copyable() = default;

    non_copyable(const non_copyable& other) = delete;
    non_copyable& operator=(const non_copyable& other) = delete;

    non_copyable(non_copyable&& other) = default;
    non_copyable& operator=(non_copyable&& other) = default;
};

template <typename T>
struct pimpl_helper {};

} // namespace detail

class window : public detail::non_copyable {
    void handle_begin();
    void handle_end();

    struct impl;
    std::unique_ptr<impl> impl;

  public:
    window(std::size_t width, std::size_t height, std::string_view name);

    VkSurfaceKHR create_surface() const;

    static constexpr std::array<const char*, 2> required_extensions();

    template <typename F>
    void handle(F&& f) {
        handle_begin();
        f();
        handle_end();
    }
};

} // namespace wsi
