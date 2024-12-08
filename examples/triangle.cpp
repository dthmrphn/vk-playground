#include <algorithm>
#include <fmt/base.h>
#include <fmt/printf.h>
#include <stdexcept>

#include <fmt/core.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// clang-format off
#include <vulkan/vulkan_raii.hpp>
// clang-format on

constexpr static const char* enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* enabled_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

constexpr std::uint32_t vert_shader_code[] = {
#include <triangle.vert.spv.hpp>
};
constexpr std::uint32_t frag_shader_code[] = {
#include <triangle.frag.spv.hpp>
};

static void check_layer(std::string_view layer) {
    const auto layers = vk::enumerateInstanceLayerProperties();
    for (const auto& l : layers) {
        if (l.layerName.data() == layer) {
            return;
        }
    }

    throw std::runtime_error(fmt::format("layer {} is not present or not installed", layer));
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                              VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
                                              void* /*pUserData*/) {
    fmt::println("{}", pCallbackData->pMessage);
    return false;
}

int main() {
    try {
        // init glfw
        std::uint32_t width = 800;
        std::uint32_t height = 600;
        glfwInit();
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        auto window = glfwCreateWindow(width, height, "vulkan", nullptr, nullptr);
        std::uint32_t count{};
        const auto exts = glfwGetRequiredInstanceExtensions(&count);

        // check if wanted layers are present
        // in system
        for (const auto& l : enabled_layers) {
            check_layer(l);
        }

        // creating instance
        vk::raii::Context context;
        vk::ApplicationInfo ai{"triangle", 1, "engine", 1, VK_API_VERSION_1_0};
        std::vector<const char*> extensions{exts, exts + count};
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        vk::raii::Instance instance{
            context,
            vk::InstanceCreateInfo{{}, &ai, 1, enabled_layers, (std::uint32_t)extensions.size(), extensions.data()},
        };

        vk::DebugUtilsMessageSeverityFlagsEXT severity_flags{vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError};
        vk::DebugUtilsMessageTypeFlagsEXT message_flags{vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation};
        vk::DebugUtilsMessengerCreateInfoEXT debug_ci({}, severity_flags, message_flags, &debug_callback);
        vk::raii::DebugUtilsMessengerEXT debug_messenger{instance, debug_ci};

        // picking physical device
        vk::raii::PhysicalDevices devs{instance};
        const auto gpu = devs.front();

        // creating surface
        VkSurfaceKHR surf;
        glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window, nullptr, &surf);
        vk::raii::SurfaceKHR surface{instance, surf};

        // find present and graphics queues indicies
        const auto props = gpu.getQueueFamilyProperties();
        const auto iter = std::find_if(props.begin(), props.end(), [](auto p) {
            return p.queueFlags & vk::QueueFlagBits::eGraphics;
        });

        std::uint32_t graphics_queue_index = std::distance(props.begin(), iter);
        std::uint32_t present_queue_index{};
        if (gpu.getSurfaceSupportKHR(graphics_queue_index, surface)) {
            present_queue_index = graphics_queue_index;
        }

        // creating logical device
        float priority{1.0f};
        vk::DeviceQueueCreateInfo qci{{}, graphics_queue_index, 1, &priority};
        vk::DeviceCreateInfo dci{{}, qci, enabled_layers, enabled_extensions};
        vk::raii::Device device{gpu, dci};

        const auto graphics_queue = device.getQueue(graphics_queue_index, 0);
        const auto present_queue = device.getQueue(present_queue_index, 0);

        // creating swapchain
        const auto formats = gpu.getSurfaceFormatsKHR(surface);
        auto format = formats[0];
        for (const auto& f : formats) {
            if (f.format == vk::Format::eB8G8R8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                format = f;
            }
        }

        const auto capabilities = gpu.getSurfaceCapabilitiesKHR(surface);
        const auto extent = vk::Extent2D{
            std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };

        const auto pre_transform = capabilities.currentTransform;
        const auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        const auto present_mode = vk::PresentModeKHR::eMailbox;

        const std::uint32_t qfi[] = {graphics_queue_index, present_queue_index};
        vk::SwapchainCreateInfoKHR sci{
            {},
            surface,
            capabilities.minImageCount + 1,
            format.format,
            format.colorSpace,
            extent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
            qfi,
            pre_transform,
            composite_alpha,
            present_mode,
            true,
        };

        vk::raii::SwapchainKHR swapchain{device, sci};
        const auto images = swapchain.getImages();

        vk::ImageViewCreateInfo ci{
            {},
            {},
            vk::ImageViewType::e2D,
            format.format,
            {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };

        std::vector<vk::raii::ImageView> image_views{};
        for (const auto& image : images) {
            ci.image = image;
            image_views.emplace_back(device, ci);
        }

        // creating render pass
        vk::AttachmentDescription attachment_desc{
            {},
            format.format,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::ePresentSrcKHR,
        };
        vk::AttachmentReference attachment_ref{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::SubpassDescription subpass_desc{{}, vk::PipelineBindPoint::eGraphics, {}, attachment_ref};
        vk::SubpassDependency subpass_dep{
            VK_SUBPASS_EXTERNAL,
            0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eNone,
            vk::AccessFlagBits::eColorAttachmentWrite,
        };
        vk::RenderPassCreateInfo rpci{
            {},
            attachment_desc,
            subpass_desc,
            subpass_dep,
        };
        vk::raii::RenderPass render_pass{device, rpci};

        // creating graphics pipeline
        const auto vert_shader = device.createShaderModule({{}, sizeof(vert_shader_code), vert_shader_code});
        const auto frag_shader = device.createShaderModule({{}, sizeof(frag_shader_code), frag_shader_code});

        vk::PipelineShaderStageCreateInfo shader_stages[] = {
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vert_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, frag_shader, "main"},
        };

        vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, vk::False};
        vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
        vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            vk::False,
            vk::False,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eBack,
            vk::FrontFace::eClockwise,
            vk::False,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, vk::False};
        vk::ColorComponentFlags color_flags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        vk::PipelineColorBlendAttachmentState colorblend_attachment{
            vk::False,
            vk::BlendFactor::eZero,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eZero,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            color_flags,
        };

        vk::PipelineColorBlendStateCreateInfo colorblend_state{
            {},
            vk::False,
            vk::LogicOp::eCopy,
            colorblend_attachment,
            {0.0f, 0.0f, 0.0f, 0.0f},
        };

        vk::DynamicState dynamic_states[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamic_state{{}, dynamic_states};
        const auto pipeline_layout = device.createPipelineLayout({{}, 0, nullptr, 0, nullptr});

        vk::GraphicsPipelineCreateInfo pci{
            {},
            shader_stages,
            &vertex_input_state,
            &input_assembly_state,
            nullptr,
            &viewport_state,
            &rasterization_state,
            &multisample_state,
            nullptr,
            &colorblend_state,
            &dynamic_state,
            pipeline_layout,
            render_pass,
        };

        const auto pipeline = device.createGraphicsPipeline(VK_NULL_HANDLE, pci);

        // creating framebuffers
        std::vector<vk::raii::Framebuffer> framebuffers{};
        for (const auto& iv : image_views) {
            vk::FramebufferCreateInfo fbci{{}, render_pass, *iv, extent.width, extent.height, 1};
            framebuffers.emplace_back(device, fbci);
        }

        // creating command pool
        vk::CommandPoolCreateFlags command_pool_flags{vk::CommandPoolCreateFlagBits::eResetCommandBuffer};
        vk::CommandPoolCreateInfo cpci{command_pool_flags, graphics_queue_index};
        vk::raii::CommandPool command_pool{device, cpci};

        // creating command buffer
        vk::CommandBufferAllocateInfo cbai{command_pool, vk::CommandBufferLevel::ePrimary, 1};
        vk::raii::CommandBuffer command_buffer{std::move(vk::raii::CommandBuffers{device, cbai}.front())};

        vk::SemaphoreCreateInfo sphci{};
        vk::raii::Semaphore image_available_semaphore{device, sphci};
        vk::raii::Semaphore render_finished_semaphore{device, sphci};
        vk::FenceCreateInfo fci{vk::FenceCreateFlagBits::eSignaled};
        vk::raii::Fence fence{device, fci};

        // render loop
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            while (vk::Result::eTimeout == device.waitForFences(*fence, vk::True, -1)) {
            }
            device.resetFences(*fence);

            const auto [_, index] = swapchain.acquireNextImage(-1, image_available_semaphore);

            command_buffer.reset();
            command_buffer.begin({});

            vk::ClearValue clear_value{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}};
            vk::RenderPassBeginInfo rpbi{render_pass, framebuffers[index], {{0, 0}, extent}, clear_value};
            command_buffer.beginRenderPass(rpbi, vk::SubpassContents::eInline);
            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            vk::Viewport viewport{0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
            command_buffer.setViewport(0, viewport);
            command_buffer.setScissor(0, vk::Rect2D{{0, 0}, extent});
            command_buffer.draw(3, 1, 0, 0);
            command_buffer.endRenderPass();
            command_buffer.end();

            const auto wait_stage_flags = vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput};
            vk::SubmitInfo submit{*image_available_semaphore, wait_stage_flags, *command_buffer, *render_finished_semaphore};
            graphics_queue.submit(submit, fence);

            vk::PresentInfoKHR present_info{*render_finished_semaphore, *swapchain, index};
            auto rv = present_queue.presentKHR(present_info);
            (void)rv;
        }

        device.waitIdle();

        glfwDestroyWindow(window);
    } catch (const std::exception& ex) {
        fmt::println("error: {}", ex.what());
    }

    return 0;
}
