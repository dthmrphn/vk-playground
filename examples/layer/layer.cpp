#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vk_layer.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

#include <layer.frag.hpp>
#include <layer.vert.hpp>

#define EXPORT_FUNCTION extern "C"

namespace layer {

template <typename KeyType, typename ValueType>
class mapping {
    std::unordered_map<KeyType, ValueType> _map;
    std::mutex _mutex;

  public:
    mapping() = default;

    ValueType& operator[](const KeyType& key) {
        std::lock_guard lg{_mutex};
        return _map[key];
    }
};

struct instance_data {
    VkuInstanceDispatchTable table{};
};

struct device_data {
    VkuDeviceDispatchTable table{};
    PFN_vkSetDeviceLoaderData set_device_loader_data{};

    VkPhysicalDevice gpu{nullptr};
    VkPhysicalDeviceProperties props{};

    VkCommandPool cmd_pool{nullptr};
    VkCommandBuffer cmd_buf{nullptr};

    VkDescriptorPool descriptor_pool{nullptr};
    VkDescriptorSet descriptor_set{nullptr};
    VkDescriptorSetLayout descriptor_layout{nullptr};

    VkPipelineLayout pipeline_layout{nullptr};

    VkImage font_image{nullptr};
    VkImageView font_image_view{nullptr};
    VkSampler font_sampler{nullptr};
    VkDeviceMemory font_image_mem{nullptr};
    bool font_uploaded{false};

    VkBuffer vertex_buffer{nullptr};
    VkDeviceMemory vertex_buffer_mem{nullptr};
    VkDeviceSize vertex_buffer_size{};

    VkBuffer index_buffer{nullptr};
    VkDeviceMemory index_buffer_mem{nullptr};
    VkDeviceSize index_buffer_size{};

    VkSemaphore semaphore{nullptr};
    VkFence fence{nullptr};
};

struct queue_data {
    VkDevice device{nullptr};
    std::uint32_t index;
    std::uint32_t family;
};

struct swapchain_data {
    VkRenderPass render_pass{nullptr};
    VkPipeline pipeline{nullptr};

    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
};

struct dispatch_data {
    VkuDeviceDispatchTable table;
};

struct physical_device_data {
    VkInstance instance{nullptr};
};

static mapping<VkInstance, instance_data> g_instance_mapping;
static mapping<VkPhysicalDevice, physical_device_data> g_physical_device_mapping;
static mapping<VkDevice, device_data> g_device_mapping;
static mapping<VkQueue, queue_data> g_queue_mapping;
static mapping<VkSwapchainKHR, swapchain_data> g_swapchain_mapping;
static mapping<void*, dispatch_data> g_dispatch_mapping;

void* get_key(const void* object) {
    return *(void**)object;
}

static std::uint32_t memory_type_index(VkPhysicalDevice gpu, std::uint32_t filter, VkMemoryPropertyFlags mask) {
    const auto instance = g_physical_device_mapping[gpu].instance;
    const auto& table = g_instance_mapping[instance].table;

    VkPhysicalDeviceMemoryProperties props{};
    table.GetPhysicalDeviceMemoryProperties(gpu, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (filter & (1 << i) && ((props.memoryTypes[i].propertyFlags & mask) == mask)) {
            return i;
        }
    }

    return 0xFFFFFFFF;
}

static std::uint32_t queue_family_index(VkPhysicalDevice gpu, VkQueueFlags flags) {
    const auto instance = g_physical_device_mapping[gpu].instance;
    const auto& table = g_instance_mapping[instance].table;

    std::uint32_t count{};
    table.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props{count};
    table.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());

    const auto iter = std::find_if(props.begin(), props.end(), [flags](auto p) {
        return p.queueFlags & flags;
    });

    return std::distance(props.begin(), iter);
}

static VkDeviceSize align_size(VkDeviceSize size, VkDeviceSize alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

static void create_buffer(VkDevice device, VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize size, VkBufferUsageFlags usage) {
    const auto& data = g_device_mapping[device];
    const auto& table = data.table;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    table.CreateBuffer(device, &bci, nullptr, buf);

    VkMemoryRequirements mr{};
    table.GetBufferMemoryRequirements(device, *buf, &mr);
    VkMemoryAllocateInfo upload_alloc_info = {};
    upload_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    upload_alloc_info.allocationSize = mr.size;
    upload_alloc_info.memoryTypeIndex = memory_type_index(data.gpu, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    table.AllocateMemory(device, &upload_alloc_info, nullptr, mem);
    table.BindBufferMemory(device, *buf, *mem, 0);
}

static void resize_buffer(VkDevice device, VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize size, VkBufferUsageFlags usage) {
    const auto& table = g_device_mapping[device].table;
    if (buf) {
        table.DestroyBuffer(device, *buf, nullptr);
    }

    if (mem) {
        table.FreeMemory(device, *mem, nullptr);
    }

    create_buffer(device, buf, mem, size, usage);
}

static void upload_fonts(VkDevice device) {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* font_data{};
    int tex_width{};
    int tex_height{};
    io.Fonts->GetTexDataAsRGBA32(&font_data, &tex_width, &tex_height);
    VkDeviceSize upload_size = tex_width * tex_height * 4 * sizeof(char);

    const auto& data = g_device_mapping[device];
    const auto& table = data.table;

    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;

    create_buffer(device, &staging_buf, &staging_mem, upload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    void* mapped = nullptr;
    table.MapMemory(device, staging_mem, 0, upload_size, 0, &mapped);
    memcpy(mapped, font_data, upload_size);
    VkMappedMemoryRange mmr[1] = {};
    mmr[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mmr[0].memory = staging_mem;
    mmr[0].size = upload_size;
    table.FlushMappedMemoryRanges(device, 1, mmr);
    table.UnmapMemory(device, staging_mem);

    VkQueue queue;
    const auto family = queue_family_index(data.gpu, VK_QUEUE_TRANSFER_BIT);
    table.GetDeviceQueue(device, family, 0, &queue);
    data.set_device_loader_data(device, queue);

    VkCommandPool cmd_pool;
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex = family;
    table.CreateCommandPool(device, &cpci, nullptr, &cmd_pool);

    VkCommandBuffer cmd_buf;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    table.AllocateCommandBuffers(device, &cbai, &cmd_buf);
    data.set_device_loader_data(device, cmd_buf);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    table.BeginCommandBuffer(cmd_buf, &cbbi);

    std::uint32_t stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = data.font_image;
    imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imb.subresourceRange.levelCount = 1;
    imb.subresourceRange.layerCount = 1;
    table.CmdPipelineBarrier(cmd_buf, stage, stage, 0, 0, nullptr, 0, nullptr, 1, &imb);

    VkBufferImageCopy bic{};
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent.width = tex_width;
    bic.imageExtent.height = tex_height;
    bic.imageExtent.depth = 1;
    table.CmdCopyBufferToImage(cmd_buf, staging_buf, data.font_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);

    imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    table.CmdPipelineBarrier(cmd_buf, stage, stage, 0, 0, nullptr, 0, nullptr, 1, &imb);

    table.EndCommandBuffer(cmd_buf);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_buf;
    table.QueueSubmit(queue, 1, &si, nullptr);
    table.QueueWaitIdle(queue);

    table.FreeCommandBuffers(device, cmd_pool, 1, &cmd_buf);
    table.DestroyCommandPool(device, cmd_pool, nullptr);
    table.FreeMemory(device, staging_mem, nullptr);
    table.DestroyBuffer(device, staging_buf, nullptr);
}

static VkLayerInstanceCreateInfo* layer_create_info(const VkInstanceCreateInfo* ici, VkLayerFunction f) {
    VkLayerInstanceCreateInfo* ci = (VkLayerInstanceCreateInfo*)ici->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerInstanceCreateInfo*)ci->pNext;
    }
    return ci;
}

static VkLayerDeviceCreateInfo* layer_create_info(const VkDeviceCreateInfo* dci, VkLayerFunction f) {
    VkLayerDeviceCreateInfo* ci = (VkLayerDeviceCreateInfo*)dci->pNext;
    while (ci && (ci->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ci->function != f)) {
        ci = (VkLayerDeviceCreateInfo*)ci->pNext;
    }
    return ci;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
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
    g_instance_mapping[*pInstance] = {table};

    std::uint32_t count{};
    table.EnumeratePhysicalDevices(*pInstance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus{count};
    table.EnumeratePhysicalDevices(*pInstance, &count, gpus.data());

    for (const auto& gpu : gpus) {
        g_physical_device_mapping[gpu] = {*pInstance};
    }

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    return rv;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    ImGui::DestroyContext();

    g_instance_mapping[instance].table.DestroyInstance(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    const auto& table = g_instance_mapping[instance].table;
    const auto rv = table.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if (rv == VK_SUCCESS && pPhysicalDevices) {
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; i++) {
            g_physical_device_mapping[pPhysicalDevices[i]] = {instance};
        }
    }

    return rv;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
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
    auto& data = g_device_mapping[*pDevice];
    data.gpu = physicalDevice;
    data.table = table;
    data.set_device_loader_data = lci->u.pfnSetDeviceLoaderData;

    const auto instance = g_physical_device_mapping[physicalDevice].instance;
    g_instance_mapping[instance].table.GetPhysicalDeviceProperties(physicalDevice, &data.props);

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

    ImGuiIO& io = ImGui::GetIO();
    unsigned char* font_data{};
    int tex_width{};
    int tex_height{};
    io.Fonts->GetTexDataAsRGBA32(&font_data, &tex_width, &tex_height);
    VkDeviceSize upload_size = tex_width * tex_height * 4 * sizeof(char);

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = tex_width;
    ici.extent.height = tex_height;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    table.CreateImage(*pDevice, &ici, pAllocator, &data.font_image);

    VkMemoryRequirements mr{};
    table.GetImageMemoryRequirements(*pDevice, data.font_image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memory_type_index(physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    table.AllocateMemory(*pDevice, &mai, pAllocator, &data.font_image_mem);
    table.BindImageMemory(*pDevice, data.font_image, data.font_image_mem, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = data.font_image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    table.CreateImageView(*pDevice, &ivci, pAllocator, &data.font_image_view);

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod = -1000;
    sci.maxLod = 1000;
    sci.maxAnisotropy = 1.0f;
    table.CreateSampler(*pDevice, &sci, pAllocator, &data.font_sampler);

    upload_fonts(*pDevice);

    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    table.CreateDescriptorPool(*pDevice, &dpci, pAllocator, &data.descriptor_pool);

    VkSampler sampler[] = {data.font_sampler};
    VkDescriptorSetLayoutBinding dslb[1] = {};
    dslb[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    dslb[0].descriptorCount = 1;
    dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dslb[0].pImmutableSamplers = sampler;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = dslb;
    table.CreateDescriptorSetLayout(*pDevice, &dslci, pAllocator, &data.descriptor_layout);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = data.descriptor_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &data.descriptor_layout;
    table.AllocateDescriptorSets(*pDevice, &dsai, &data.descriptor_set);

    VkDescriptorImageInfo dii[1] = {};
    dii[0].sampler = data.font_sampler;
    dii[0].imageView = data.font_image_view;
    dii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wds[1] = {};
    wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds[0].dstSet = data.descriptor_set;
    wds[0].descriptorCount = 1;
    wds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds[0].pImageInfo = dii;
    table.UpdateDescriptorSets(*pDevice, 1, wds, 0, nullptr);

    VkPushConstantRange pcr[1] = {};
    pcr[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr[0].offset = sizeof(float) * 0;
    pcr[0].size = sizeof(float) * 4;
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &data.descriptor_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = pcr;
    table.CreatePipelineLayout(*pDevice, &plci, pAllocator, &data.pipeline_layout);

    return rv;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    const auto& data = g_device_mapping[device];
    const auto& table = data.table;

    table.FreeMemory(device, data.index_buffer_mem, nullptr);
    table.DestroyBuffer(device, data.index_buffer, nullptr);
    table.FreeMemory(device, data.vertex_buffer_mem, nullptr);
    table.DestroyBuffer(device, data.vertex_buffer, nullptr);
    table.DestroyPipelineLayout(device, data.pipeline_layout, pAllocator);
    table.DestroyDescriptorSetLayout(device, data.descriptor_layout, pAllocator);
    table.DestroyDescriptorPool(device, data.descriptor_pool, pAllocator);
    table.DestroySampler(device, data.font_sampler, pAllocator);
    table.DestroyImageView(device, data.font_image_view, pAllocator);
    table.DestroyImage(device, data.font_image, pAllocator);
    table.FreeMemory(device, data.font_image_mem, pAllocator);
    table.DestroySemaphore(device, data.semaphore, pAllocator);
    table.DestroyFence(device, data.fence, pAllocator);
    table.FreeCommandBuffers(device, data.cmd_pool, 1, &data.cmd_buf);
    table.DestroyCommandPool(device, data.cmd_pool, pAllocator);

    table.DestroyDevice(device, pAllocator);
}

VKAPI_PTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    const auto& dd = g_device_mapping[device];
    const auto& table = dd.table;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    const auto rv = table.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (rv == VK_SUCCESS) {
        auto& sd = g_swapchain_mapping[*pSwapchain];

        VkAttachmentDescription attach_desc{};
        attach_desc.format = pCreateInfo->imageFormat;
        attach_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        attach_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
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

        VkShaderModule vert, frag;
        {
            VkShaderModuleCreateInfo smci{};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.pCode = layer_vert::code;
            smci.codeSize = layer_vert::size;
            table.CreateShaderModule(device, &smci, pAllocator, &vert);
        }
        {
            VkShaderModuleCreateInfo smci{};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.pCode = layer_frag::code;
            smci.codeSize = layer_frag::size;
            table.CreateShaderModule(device, &smci, pAllocator, &frag);
        }

        VkPipelineShaderStageCreateInfo pssci[2] = {};
        pssci[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pssci[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        pssci[0].module = vert;
        pssci[0].pName = "main";
        pssci[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pssci[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        pssci[1].module = frag;
        pssci[1].pName = "main";

        VkVertexInputBindingDescription vibd[1] = {};
        vibd[0].stride = sizeof(ImDrawVert);
        vibd[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription viad[3] = {};
        viad[0].location = 0;
        viad[0].binding = vibd[0].binding;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = IM_OFFSETOF(ImDrawVert, pos);
        viad[1].location = 1;
        viad[1].binding = vibd[0].binding;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = IM_OFFSETOF(ImDrawVert, uv);
        viad[2].location = 2;
        viad[2].binding = vibd[0].binding;
        viad[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        viad[2].offset = IM_OFFSETOF(ImDrawVert, col);

        VkPipelineVertexInputStateCreateInfo pvisci{};
        pvisci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        pvisci.vertexBindingDescriptionCount = 1;
        pvisci.pVertexBindingDescriptions = vibd;
        pvisci.vertexAttributeDescriptionCount = 3;
        pvisci.pVertexAttributeDescriptions = viad;

        VkPipelineInputAssemblyStateCreateInfo piasci{};
        piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        piasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo pvsci{};
        pvsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        pvsci.viewportCount = 1;
        pvsci.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo prsci{};
        prsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        prsci.polygonMode = VK_POLYGON_MODE_FILL;
        prsci.cullMode = VK_CULL_MODE_NONE;
        prsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        prsci.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo pmsci{};
        pmsci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        pmsci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState pcbas[1] = {};
        pcbas[0].blendEnable = VK_TRUE;
        pcbas[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        pcbas[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        pcbas[0].colorBlendOp = VK_BLEND_OP_ADD;
        pcbas[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        pcbas[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        pcbas[0].alphaBlendOp = VK_BLEND_OP_ADD;
        pcbas[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                  VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineDepthStencilStateCreateInfo pdssci{};
        pdssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendStateCreateInfo pcbsci{};
        pcbsci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        pcbsci.attachmentCount = 1;
        pcbsci.pAttachments = pcbas;

        VkDynamicState dss[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo pdsci{};
        pdsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        pdsci.dynamicStateCount = (uint32_t)IM_ARRAYSIZE(dss);
        pdsci.pDynamicStates = dss;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.flags = 0;
        gpci.stageCount = 2;
        gpci.pStages = pssci;
        gpci.pVertexInputState = &pvisci;
        gpci.pInputAssemblyState = &piasci;
        gpci.pViewportState = &pvsci;
        gpci.pRasterizationState = &prsci;
        gpci.pMultisampleState = &pmsci;
        gpci.pDepthStencilState = &pdssci;
        gpci.pColorBlendState = &pcbsci;
        gpci.pDynamicState = &pdsci;
        gpci.layout = dd.pipeline_layout;
        gpci.renderPass = sd.render_pass;
        table.CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, pAllocator, &sd.pipeline);
        table.DestroyShaderModule(device, vert, pAllocator);
        table.DestroyShaderModule(device, frag, pAllocator);
    }

    return rv;
}

VKAPI_PTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    auto& sd = g_swapchain_mapping[swapchain];
    const auto& table = g_device_mapping[device].table;

    table.DestroyPipeline(device, sd.pipeline, pAllocator);

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
    const auto device = g_queue_mapping[queue].device;
    auto& data = g_device_mapping[device];
    const auto& table = data.table;

    VkResult rv{VK_SUCCESS};

    for (std::size_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        auto swapchain = pPresentInfo->pSwapchains[i];
        auto image_index = pPresentInfo->pImageIndices[i];
        auto sd = g_swapchain_mapping[swapchain];

        ImGui::NewFrame();
        ImGui::ShowDemoWindow();
        ImGui::Render();

        const auto draw_data = ImGui::GetDrawData();

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
        rpbi.renderArea.extent.width = draw_data->DisplaySize.x;
        rpbi.renderArea.extent.height = draw_data->DisplaySize.y;
        table.CmdBeginRenderPass(data.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        const auto alignment = data.props.limits.nonCoherentAtomSize;
        const auto vtx_size = align_size(draw_data->TotalVtxCount * sizeof(ImDrawVert), alignment);
        const auto idx_size = align_size(draw_data->TotalIdxCount * sizeof(ImDrawIdx), alignment);

        if (vtx_size > 0 && idx_size > 0) {
            if (data.vertex_buffer_size < vtx_size) {
                resize_buffer(device, &data.vertex_buffer, &data.vertex_buffer_mem, vtx_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                data.vertex_buffer_size = vtx_size;
            }

            if (data.index_buffer_size < idx_size) {
                resize_buffer(device, &data.index_buffer, &data.index_buffer_mem, idx_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
                data.index_buffer_size = idx_size;
            }

            void* vtx_map{};
            table.MapMemory(device, data.vertex_buffer_mem, 0, vtx_size, 0, &vtx_map);
            void* idx_map{};
            table.MapMemory(device, data.index_buffer_mem, 0, idx_size, 0, &idx_map);

            ImDrawVert* draw_vtx = static_cast<ImDrawVert*>(vtx_map);
            ImDrawIdx* draw_idx = static_cast<ImDrawIdx*>(idx_map);

            for (std::size_t i = 0; i < draw_data->CmdListsCount; i++) {
                const ImDrawList* cmd_list = draw_data->CmdLists[i];
                memcpy(draw_vtx, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                memcpy(draw_idx, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
                draw_vtx += cmd_list->VtxBuffer.Size;
                draw_idx += cmd_list->IdxBuffer.Size;
            }

            VkMappedMemoryRange mmrs[2] = {};
            mmrs[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mmrs[0].memory = data.vertex_buffer_mem;
            mmrs[0].size = VK_WHOLE_SIZE;
            mmrs[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mmrs[1].memory = data.index_buffer_mem;
            mmrs[1].size = VK_WHOLE_SIZE;
            table.FlushMappedMemoryRanges(device, 2, mmrs);
            table.UnmapMemory(device, data.vertex_buffer_mem);
            table.UnmapMemory(device, data.index_buffer_mem);
        }

        table.CmdBindPipeline(data.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, sd.pipeline);
        table.CmdBindDescriptorSets(data.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, data.pipeline_layout, 0, 1, &data.descriptor_set, 0, nullptr);

        if (vtx_size) {
            VkDeviceSize offsets[] = {0};
            table.CmdBindVertexBuffers(data.cmd_buf, 0, 1, &data.vertex_buffer, offsets);
            table.CmdBindIndexBuffer(data.cmd_buf, data.index_buffer, 0, VK_INDEX_TYPE_UINT16);
        }

        VkViewport vp{};
        vp.x = 0;
        vp.y = 0;
        vp.width = draw_data->DisplaySize.x;
        vp.height = draw_data->DisplaySize.y;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        table.CmdSetViewport(data.cmd_buf, 0, 1, &vp);

        float scale[2];
        scale[0] = 2.0f / draw_data->DisplaySize.x;
        scale[1] = 2.0f / draw_data->DisplaySize.y;
        float translate[2];
        translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
        translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
        table.CmdPushConstants(data.cmd_buf, data.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0, sizeof(float) * 2, scale);

        table.CmdPushConstants(data.cmd_buf, data.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

        int32_t vtx_offset = 0;
        int32_t idx_offset = 0;

        if (draw_data->CmdListsCount > 0) {
            for (int32_t i = 0; i < draw_data->CmdListsCount; i++) {
                const ImDrawList* cmd_list = draw_data->CmdLists[i];
                for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
                    VkRect2D scissorRect;
                    scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
                    scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
                    scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
                    scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
                    table.CmdSetScissor(data.cmd_buf, 0, 1, &scissorRect);
                    table.CmdDrawIndexed(data.cmd_buf, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
                    idx_offset += pcmd->ElemCount;
                }
                vtx_offset += cmd_list->VtxBuffer.Size;
            }
        }

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent.width = draw_data->DisplaySize.x;
        scissor.extent.height = draw_data->DisplaySize.y;
        table.CmdSetScissor(data.cmd_buf, 0, 1, &scissor);

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
    const auto& table = g_device_mapping[device].table;

    table.GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);

    g_queue_mapping[*pQueue] = {device, queueIndex, queueFamilyIndex};
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

    HOOK(vkEnumeratePhysicalDevices);

    HOOK(vkCreateDevice);
    HOOK(vkDestroyDevice);

    HOOK(vkCreateSwapchainKHR);
    HOOK(vkDestroySwapchainKHR);

    HOOK(vkQueuePresentKHR);
    HOOK(vkGetDeviceQueue);

    return layer::g_instance_mapping[inst].table.GetInstanceProcAddr(inst, name);
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

    return layer::g_device_mapping[dev].table.GetDeviceProcAddr(dev, name);
}

#undef HOOK
