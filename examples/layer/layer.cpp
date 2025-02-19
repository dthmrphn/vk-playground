#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vk_layer.h>

#include <fmt/core.h>

#include <unordered_map>

#define EXPORT_FUNCTION extern "C"

namespace layer {

std::unordered_map<void*, VkuInstanceDispatchTable> g_instance_table;
std::unordered_map<void*, VkuDeviceDispatchTable> g_device_table;

VkLayerInstanceCreateInfo* layerCreateInfo(const VkInstanceCreateInfo* ici, VkLayerFunction f) {
    VkLayerInstanceCreateInfo* ci = (VkLayerInstanceCreateInfo*)ici->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerInstanceCreateInfo*)ci->pNext;
    }
    return ci;
}

VkLayerDeviceCreateInfo* layerCreateInfo(const VkDeviceCreateInfo* dci, VkLayerFunction f) {
    VkLayerDeviceCreateInfo* ci = (VkLayerDeviceCreateInfo*)dci->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerDeviceCreateInfo*)ci->pNext;
    }
    return ci;
}

void* getKey(const void* object) { return *(void**)object; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    auto lci = layerCreateInfo(pCreateInfo, VK_LAYER_LINK_INFO);
    if (lci == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const auto gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto f_create_inst = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (f_create_inst == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;
    VkResult rv = f_create_inst(pCreateInfo, pAllocator, pInstance);
    if (rv != VK_SUCCESS) {
        return rv;
    }

    VkuInstanceDispatchTable table{};
    vkuInitInstanceDispatchTable(*pInstance, &table, gipa);
    g_instance_table[getKey(*pInstance)] = table;

    return rv;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    auto lci = layerCreateInfo(pCreateInfo, VK_LAYER_LINK_INFO);
    if (lci == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const auto gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto gdpa = lci->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;

    const auto f_create_dev = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    if (f_create_dev == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult rv = f_create_dev(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (rv != VK_SUCCESS) {
        return rv;
    }

    VkuDeviceDispatchTable table{};
    vkuInitDeviceDispatchTable(*pDevice, &table, gdpa);
    g_device_table[getKey(*pDevice)] = table;

    return rv;
}

} // namespace layer

// clang-format off
#define HOOK(f) if (!std::strcmp(name, #f)) { return reinterpret_cast<PFN_vkVoidFunction>(layer::f); }
// clang-format on

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char* name) {
    if (!std::strcmp(name, "vkGetInstanceProcAddr")) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    }
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }

    HOOK(vkCreateInstance);
    HOOK(vkCreateDevice);
    
    return layer::g_instance_table[layer::getKey(inst)].GetInstanceProcAddr(inst, name);
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char* name) {
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }

    HOOK(vkCreateDevice);

    return layer::g_device_table[layer::getKey(dev)].GetDeviceProcAddr(dev, name);
}

#undef HOOK
