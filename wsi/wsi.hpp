#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

#include <utility>
#include <vulkan/vulkan_core.h>

namespace wsi {

namespace detail {

template <typename T, std::size_t Size, std::size_t Alignment = sizeof(void*)>
struct pimpl {
    pimpl(const pimpl&) = delete;
    pimpl& operator=(const pimpl&) = delete;

    pimpl(pimpl&&) = default;
    pimpl& operator=(pimpl&&) = default;

    template <typename... Args>
    pimpl(Args&&... args) {
        new (ptr()) T(std::forward<Args>(args)...);
    }

    T& operator*() noexcept { return *ptr(); }
    const T& operator*() const noexcept { return *ptr(); }

    T* operator->() noexcept { return ptr(); }
    const T* operator->() const noexcept { return ptr(); }
    
    ~pimpl() {
        check<sizeof(T), alignof(T)>();
        ptr()->~T();
    }

  private:
    std::byte _storage[Size];

    T* ptr() { return reinterpret_cast<T*>(&_storage[0]); }

    template <std::size_t RealSize, std::size_t RealAlignment>
    static void check() noexcept {
        static_assert(RealSize == Size, "size mismatch");
        static_assert(RealAlignment == Alignment, "alignment mismatch");
    }
};

} // namespace detail

class window {
  public:
    struct platform;

    window(std::size_t width, std::size_t height, std::string_view name);

    VkSurfaceKHR create_surface() const;

    static constexpr std::array<const char*, 2> required_extensions();

  private:
    detail::pimpl<platform, 96> _impl;
};

} // namespace wsi
