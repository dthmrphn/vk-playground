#include <stdexcept>
#include <vulkan/vulkan_raii.hpp>

#include <fmt/core.h>

constexpr static vk::ApplicationInfo app_info = {
    "select-device",
    1,
    "engine",
    1,
    VK_API_VERSION_1_3,
};

std::string driver_version(const vk::PhysicalDeviceProperties& props) {
    const auto driver = props.driverVersion;
    const auto vendor = props.vendorID;

    if (vendor == 4318) {
        return fmt::format("{}.{}.{}.{}", (driver >> 22) & 0x3ff, (driver >> 14) & 0x0ff, (driver >> 6) & 0x0ff, (driver)&0x003f);
    }

#if defined(WIN32)
    if (vendor == 0x8086) {
        return fmt::format("{}.{}", (driver >> 14), (driver)&0x3fff);
    }
#endif

    return fmt::format("{}.{}.{}", (driver >> 22), (driver >> 12) & 0x3ff, (driver & 0xfff));
}

void device_info(const vk::PhysicalDeviceProperties& props) {
    fmt::print("name: {}\n", props.deviceName.data());
    fmt::print("type: {}\n", vk::to_string(props.deviceType));
    const auto major = VK_VERSION_MAJOR(props.apiVersion);
    const auto minor = VK_VERSION_MINOR(props.apiVersion);
    const auto patch = VK_VERSION_PATCH(props.apiVersion);
    fmt::print("api version: {}.{}.{}\n", major, minor, patch);
    fmt::print("driver version: {}\n", driver_version(props));
}

void device_info2(const vk::PhysicalDevice& dev) {
    const auto chain = dev.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    const auto& props2 = chain.get<vk::PhysicalDeviceProperties2>();
    const auto& dri_props = chain.get<vk::PhysicalDeviceDriverProperties>();

    device_info(props2.properties);
    fmt::print("driver info: {} {} {}\n", vk::to_string(dri_props.driverID), dri_props.driverInfo.data(), dri_props.driverName.data());
}

vk::raii::PhysicalDevice pick_device(const vk::raii::Instance& instance) {
    vk::raii::PhysicalDevice discrete{nullptr};
    vk::raii::PhysicalDevice integrated{nullptr};

    for (const auto& d : instance.enumeratePhysicalDevices()) {
        const auto props = d.getProperties();
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            discrete = d;
        }

        if (props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
            integrated = d;
        }
    }

    if (*discrete) {
        return discrete;
    }

    if (*integrated) {
        return integrated;
    }

    throw std::runtime_error("suitable device not found");
}

int main() {
    try {
        vk::raii::Context context;
        vk::raii::Instance instance{
            context,
            vk::InstanceCreateInfo{{}, &app_info, nullptr, nullptr},
        };

        const auto dev = pick_device(instance);
        fmt::print("found suitable device:\n");
        device_info2(dev);

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
        return 1;
    }

    return 0;
}
