#include <fmt/base.h>
#include <mutex>
#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vk_layer.h>

#include <fmt/core.h>

#include <unordered_map>
#include <vector>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

#define EXPORT_FUNCTION extern "C"

namespace layer {

struct instance_data {
    VkuInstanceDispatchTable table;
};

struct device_data {
    VkuDeviceDispatchTable table;

    PFN_vkSetDeviceLoaderData set_device_loader_data;

    VkPhysicalDevice gpu;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buf;

    VkSemaphore semaphore;
    VkFence fence;
};

struct queue_data {
    VkDevice device;
    std::uint32_t index;
    std::uint32_t family;
};

struct swapchain_data {
    VkRenderPass render_pass;

    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
};

struct dispatch_data {
    VkuDeviceDispatchTable table;
};

static std::mutex g_mutex;

static std::unordered_map<void*, instance_data> g_instance_data;
static std::unordered_map<void*, device_data> g_device_data;
static std::unordered_map<void*, queue_data> g_queue_data;
static std::unordered_map<void*, swapchain_data> g_swapchain_data;
static std::unordered_map<void*, dispatch_data> g_dispatch_data;

void* get_key(const void* object) {
    return *(void**)object;
}

VkLayerInstanceCreateInfo* layer_create_info(const VkInstanceCreateInfo* ici, VkLayerFunction f) {
    VkLayerInstanceCreateInfo* ci = (VkLayerInstanceCreateInfo*)ici->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerInstanceCreateInfo*)ci->pNext;
    }
    return ci;
}

VkLayerDeviceCreateInfo* layer_create_info(const VkDeviceCreateInfo* dci, VkLayerFunction f) {
    VkLayerDeviceCreateInfo* ci = (VkLayerDeviceCreateInfo*)dci->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerDeviceCreateInfo*)ci->pNext;
    }
    return ci;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    std::lock_guard lg{g_mutex};

    auto lci = layer_create_info(pCreateInfo, VK_LAYER_LINK_INFO);
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
    g_instance_data[*pInstance] = {table};

    return rv;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    std::lock_guard lg{g_mutex};

    g_instance_data[instance].table.DestroyInstance(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    std::lock_guard lg{g_mutex};

    auto lci = layer_create_info(pCreateInfo, VK_LAYER_LINK_INFO);
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

    lci = layer_create_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
    if (lci == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkuDeviceDispatchTable table{};
    vkuInitDeviceDispatchTable(*pDevice, &table, gdpa);
    auto& data = g_device_data[*pDevice];
    data.gpu = physicalDevice;
    data.table = table;
    data.set_device_loader_data = lci->u.pfnSetDeviceLoaderData;

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = 0; // TODO
    table.CreateCommandPool(*pDevice, &cpci, pAllocator, &data.cmd_pool);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = data.cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    table.AllocateCommandBuffers(*pDevice, &cbai, &data.cmd_buf);
    data.set_device_loader_data(*pDevice, data.cmd_buf);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    table.CreateSemaphore(*pDevice, &si, pAllocator, &data.semaphore);

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    table.CreateFence(*pDevice, &fi, pAllocator, &data.fence);

    return rv;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    std::lock_guard lg{g_mutex};

    const auto& data = g_device_data[device];
    const auto& table = data.table;

    table.DestroySemaphore(device, data.semaphore, pAllocator);
    table.DestroyFence(device, data.fence, pAllocator);
    table.FreeCommandBuffers(device, data.cmd_pool, 1, &data.cmd_buf);
    table.DestroyCommandPool(device, data.cmd_pool, pAllocator);

    table.DestroyDevice(device, pAllocator);
}

VKAPI_PTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    std::lock_guard lg{g_mutex};

    const auto& table = g_device_data[device].table;

    const auto rv = table.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (rv == VK_SUCCESS) {
        auto& sd = g_swapchain_data[*pSwapchain];

        VkAttachmentDescription attach_desc{};
        attach_desc.format = pCreateInfo->imageFormat;
        attach_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        attach_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attach_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attach_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attach_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attach_desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attach_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference attach_ref{};
        attach_ref.attachment = 0;
        attach_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &attach_ref;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1;
        rpi.pAttachments = &attach_desc;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &subpass;
        rpi.dependencyCount = 1;
        rpi.pDependencies = &dependency;
        table.CreateRenderPass(device, &rpi, pAllocator, &sd.render_pass);

        std::uint32_t count{};
        table.GetSwapchainImagesKHR(device, *pSwapchain, &count, nullptr);
        sd.images.resize(count);
        table.GetSwapchainImagesKHR(device, *pSwapchain, &count, sd.images.data());

        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = pCreateInfo->imageFormat;
        ci.components.r = VK_COMPONENT_SWIZZLE_R;
        ci.components.g = VK_COMPONENT_SWIZZLE_G;
        ci.components.b = VK_COMPONENT_SWIZZLE_B;
        ci.components.a = VK_COMPONENT_SWIZZLE_A;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        sd.image_views.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            ci.image = sd.images[i];
            table.CreateImageView(device, &ci, pAllocator, &sd.image_views[i]);
        }

        sd.framebuffers.resize(count);
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = sd.render_pass;
        fbci.attachmentCount = 1;
        fbci.width = pCreateInfo->imageExtent.width;
        fbci.height = pCreateInfo->imageExtent.height;
        fbci.layers = 1;
        for (std::size_t i = 0; i < count; ++i) {
            VkImageView attachment[] = {sd.image_views[i]};
            fbci.pAttachments = attachment;
            table.CreateFramebuffer(device, &fbci, pAllocator, &sd.framebuffers[i]);
        }
    }

    return rv;
}

VKAPI_PTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    std::lock_guard lg{g_mutex};

    auto& sd = g_swapchain_data[swapchain];
    const auto& table = g_device_data[device].table;

    for (auto& iv : sd.image_views) {
        table.DestroyImageView(device, iv, pAllocator);
    }

    for (auto& fb : sd.framebuffers) {
        table.DestroyFramebuffer(device, fb, pAllocator);
    }

    table.DestroyRenderPass(device, sd.render_pass, pAllocator);

    return table.DestroySwapchainKHR(device, swapchain, pAllocator);
}

VKAPI_PTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    std::lock_guard lg{g_mutex};

    const auto device = g_queue_data[queue].device;
    const auto& data = g_device_data[device];
    const auto& table = data.table;

    VkResult rv{VK_SUCCESS};

    for (std::size_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        auto swapchain = pPresentInfo->pSwapchains[i];
        auto image_index = pPresentInfo->pImageIndices[i];
        auto sd = g_swapchain_data[swapchain];

        while (VK_TIMEOUT == table.WaitForFences(device, 1, &data.fence, VK_TRUE, -1)) {
        }
        table.ResetFences(device, 1, &data.fence);

        table.ResetCommandBuffer(data.cmd_buf, 0);

        VkCommandBufferBeginInfo cbbi{};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        table.BeginCommandBuffer(data.cmd_buf, &cbbi);

        VkImageMemoryBarrier imb;
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.pNext = nullptr;
        imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imb.image = sd.images[image_index];
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;
        imb.srcQueueFamilyIndex = 0;
        imb.dstQueueFamilyIndex = 0;
        table.CmdPipelineBarrier(data.cmd_buf,
                                 VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                 VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                 0,          /* dependency flags */
                                 0, nullptr, /* memory barriers */
                                 0, nullptr, /* buffer memory barriers */
                                 1, &imb);   /* image memory barriers */

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = sd.render_pass;
        rpbi.framebuffer = sd.framebuffers[image_index];
        rpbi.renderArea.offset.x = 400;
        rpbi.renderArea.offset.y = 400;
        rpbi.renderArea.extent.width = 100;
        rpbi.renderArea.extent.height = 100;
        VkClearValue cv[1];
        cv[0].color = {0.9f, 0.1f, 0.2f, 0.5f};
        cv[0].depthStencil = {1.0f, 0};
        rpbi.pClearValues = cv;
        rpbi.clearValueCount = 1;
        table.CmdBeginRenderPass(data.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        table.CmdEndRenderPass(data.cmd_buf);
        table.EndCommandBuffer(data.cmd_buf);

        std::vector<VkPipelineStageFlags> stages_wait(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &data.cmd_buf;
        si.pWaitDstStageMask = stages_wait.data();
        si.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
        si.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &data.semaphore;
        table.QueueSubmit(queue, 1, &si, data.fence);

        VkPresentInfoKHR pi = *pPresentInfo;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &image_index;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &data.semaphore;

        rv = table.QueuePresentKHR(queue, &pi);
    }

    return rv;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    std::lock_guard lg{g_mutex};

    const auto& table = g_device_data[device].table;

    table.GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);

    g_queue_data[*pQueue] = {device, queueIndex, queueFamilyIndex};
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
    HOOK(vkDestroyInstance);

    HOOK(vkCreateDevice);
    HOOK(vkDestroyDevice);

    HOOK(vkCreateSwapchainKHR);
    HOOK(vkDestroySwapchainKHR);

    HOOK(vkQueuePresentKHR);
    HOOK(vkGetDeviceQueue);

    return layer::g_instance_data[inst].table.GetInstanceProcAddr(inst, name);
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char* name) {
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }

    HOOK(vkCreateDevice);
    HOOK(vkDestroyDevice);

    HOOK(vkCreateSwapchainKHR);
    HOOK(vkDestroySwapchainKHR);

    HOOK(vkQueuePresentKHR);
    HOOK(vkGetDeviceQueue);

    return layer::g_device_data[dev].table.GetDeviceProcAddr(dev, name);
}

#undef HOOK
