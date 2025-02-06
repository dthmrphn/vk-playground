#include <vulkan/vulkan_raii.hpp>

#include <fmt/core.h>

constexpr static vk::ApplicationInfo app_info = {
    "select-device",
    1,
    "engine",
    1,
    VK_API_VERSION_1_0,
};

void device_info(const vk::raii::PhysicalDevice& dev) {
    const auto props = dev.getProperties();
    fmt::print("name: {}\n", props.deviceName.data());
    fmt::print("type: {}\n", vk::to_string(props.deviceType));
    const auto major = VK_VERSION_MAJOR(props.apiVersion);
    const auto minor = VK_VERSION_MINOR(props.apiVersion);
    const auto patch = VK_VERSION_PATCH(props.apiVersion);
    fmt::print("api: {}.{}.{}\n", major, minor, patch);
    fmt::print("driver: {}\n", props.driverVersion);
    fmt::print("\n");
}

vk::raii::PhysicalDevice pick_device(const vk::raii::PhysicalDevices& devs) {
    for (const auto& d : devs) {
        const auto props = d.getProperties();
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            return d;
        }
    }

    return nullptr;
}

int main() {
    vk::raii::Context context;
    vk::raii::Instance instance{
        context,
        vk::InstanceCreateInfo{{}, &app_info, nullptr, nullptr},
    };

    vk::raii::PhysicalDevices devs{instance};
    const auto dev = pick_device(devs);
}
