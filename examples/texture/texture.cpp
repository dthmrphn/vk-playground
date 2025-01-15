#include "application.hpp"

#include <fmt/core.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <texture.frag.hpp>
#include <texture.vert.hpp>

struct vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 coord;

    static constexpr vk::VertexInputBindingDescription binding_desc() {
        return {0, sizeof(vertex), vk::VertexInputRate::eVertex};
    }

    static constexpr std::array<vk::VertexInputAttributeDescription, 3> attribute_desc() {
        return {
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex, pos)},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex, color)},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat, offsetof(vertex, coord)},
        };
    }
};

struct uniform {
    glm::mat4 m;
    glm::mat4 v;
    glm::mat4 p;

    static constexpr vk::DescriptorSetLayoutBinding layout_binding() {
        return {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex};
    }
};

struct texture : public common::application<texture> {
    vk::raii::Pipeline _pipeline{nullptr};
    vk::raii::PipelineLayout _pipeline_layout{nullptr};

    vulkan::device_buffer _verticies_buffer;
    vulkan::device_buffer _indices_buffer;
    vulkan::host_buffer _uniform_buffer;
    vulkan::texture _texture;

    vk::raii::DescriptorSetLayout _descriptor_layout{nullptr};
    vk::raii::DescriptorPool _descriptor_pool{nullptr};
    vk::raii::DescriptorSet _descriptor_set{nullptr};

    texture() : common::application<texture>({"texture", 1, "engine", 1, VK_API_VERSION_1_0}, 800, 600) {
        make_vertex_buffer();
        make_indices_buffer();
        make_texture_image();

        _uniform_buffer = {
            _device,
            sizeof(uniform),
            vk::BufferUsageFlagBits::eUniformBuffer,
        };

        vk::DescriptorSetLayoutBinding bindings[] = {
            uniform::layout_binding(),
            vulkan::texture::layout_binding(1),
        };

        vk::DescriptorSetLayoutCreateInfo dslci{{}, bindings};
        _descriptor_layout = _device.make_descriptor_set_layout(dslci);

        vk::DescriptorPoolSize sizes[] = {
            {vk::DescriptorType::eUniformBuffer, 1},
            {vk::DescriptorType::eCombinedImageSampler, 1},
        };

        vk::DescriptorPoolCreateInfo dpci{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, sizes};
        _descriptor_pool = _device.make_descriptor_pool(dpci);

        vk::DescriptorSetAllocateInfo dsai{_descriptor_pool, *_descriptor_layout};
        _descriptor_set = std::move(_device.make_descriptor_sets(dsai).front());

        vk::DescriptorBufferInfo dbi{_uniform_buffer.buf(), 0, sizeof(uniform)};
        vk::DescriptorImageInfo dii{_texture.sampler(), _texture.view(), vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet wdss[] = {
            {_descriptor_set, 0, 0, vk::DescriptorType::eUniformBuffer, {}, dbi},
            {_descriptor_set, 1, 0, vk::DescriptorType::eCombinedImageSampler, dii},
        };
        _device.logical().updateDescriptorSets(wdss, nullptr);

        const auto vert_shader = _device.make_shader_module({{}, texture_vert::size, texture_vert::code});
        const auto frag_shader = _device.make_shader_module({{}, texture_frag::size, texture_frag::code});

        vk::PipelineShaderStageCreateInfo shader_stages[] = {
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vert_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, frag_shader, "main"},
        };

        constexpr auto binding_desc = vertex::binding_desc();
        constexpr auto attribute_desc = vertex::attribute_desc();
        vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, binding_desc, attribute_desc};
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, vk::False};
        vk::PipelineDepthStencilStateCreateInfo depth_state{{}, true, true, vk::CompareOp::eLess, false, false};

        vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
        vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            vk::False,
            vk::False,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eBack,
            vk::FrontFace::eCounterClockwise,
            vk::False,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, vk::False};
        vk::ColorComponentFlags color_flags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        vk::PipelineColorBlendAttachmentState colorblend_attachment{
            vk::True,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
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

        vk::PipelineLayoutCreateInfo plci{{}, *_descriptor_layout};
        _pipeline_layout = _device.make_pipeline_layout(plci);

        vk::GraphicsPipelineCreateInfo pci{
            {},
            shader_stages,
            &vertex_input_state,
            &input_assembly_state,
            nullptr,
            &viewport_state,
            &rasterization_state,
            &multisample_state,
            &depth_state,
            &colorblend_state,
            &dynamic_state,
            _pipeline_layout,
            _render_pass,
        };

        _pipeline = _device.make_pipeline(pci);
    }

    void make_vertex_buffer() {
        std::array<vertex, 8> verticies = {{
            {{-0.5, +0.5, +0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0}},
            {{+0.5, +0.5, +0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0}},
            {{+0.5, -0.5, +0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0}},
            {{-0.5, -0.5, +0.0}, {0.5, 0.5, 0.5}, {0.0, 0.0}},

            {{-0.5, +0.5, -0.5}, {1.0, 0.0, 0.0}, {0.0, 1.0}},
            {{+0.5, +0.5, -0.5}, {0.0, 1.0, 0.0}, {1.0, 1.0}},
            {{+0.5, -0.5, -0.5}, {0.0, 0.0, 1.0}, {1.0, 0.0}},
            {{-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5}, {0.0, 0.0}},
        }};

        constexpr auto size = sizeof(vertex) * verticies.size();
        vulkan::host_buffer staging{
            _device,
            size,
            vk::BufferUsageFlagBits::eTransferSrc,
            verticies.data(),
        };

        _verticies_buffer = {
            _device,
            size,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        };

        _device.copy_buffers(staging.buf(), _verticies_buffer.buf(), size);
    }

    void make_indices_buffer() {
        std::array<std::uint32_t, 12> indicies = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};

        constexpr auto size = sizeof(std::uint32_t) * indicies.size();
        vulkan::host_buffer staging{
            _device,
            size,
            vk::BufferUsageFlagBits::eTransferSrc,
            indicies.data(),
        };

        _indices_buffer = {
            _device,
            size,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        };

        _device.copy_buffers(staging.buf(), _indices_buffer.buf(), size);
    }

    void make_texture_image() {
        int w{}, h{}, c{}, wc{4};
        auto data = stbi_load("textures/vulkan.png", &w, &h, &c, wc);
        if (!data) {
            throw std::runtime_error("failed to load image");
        }

        std::uint32_t width = w;
        std::uint32_t height = h;

        const vk::DeviceSize size = w * h * wc;
        vulkan::host_buffer staging{
            _device,
            size,
            vk::BufferUsageFlagBits::eTransferSrc,
            data,
        };

        _texture = {_device, width, height};

        _device.copy_buffer_to_image(staging.buf(), _texture.image(), _texture.extent());
    }

    void render() {
        auto i = acquire();

        const auto& cb = _frames[_current_frame].command_buffer;
        const auto [w, h] = _swapchain.extent();
        const float time = glfwGetTime();
        uniform ubo{
            glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::lookAt(glm::vec3(1.0f, 1.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::perspective(glm::radians(45.0f), (float)w / h, 1.0f, 10.0f),
        };
        _uniform_buffer.copy(&ubo, sizeof(ubo));

        vk::ClearValue clear_values[] = {
            vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f},
            vk::ClearDepthStencilValue{1.0f, 0},
        };
        vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[i], {{0, 0}, _swapchain.extent()}, clear_values};
        vk::Viewport viewport{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};

        cb.reset();
        cb.begin({});
        cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _pipeline_layout, 0, {_descriptor_set}, nullptr);
        cb.bindVertexBuffers(0, *_verticies_buffer.buf(), {0});
        cb.bindIndexBuffer(_indices_buffer.buf(), 0, vk::IndexType::eUint32);
        cb.setViewport(0, viewport);
        cb.setScissor(0, vk::Rect2D{{0, 0}, _swapchain.extent()});
        cb.drawIndexed(12, 1, 0, 0, 0);
        cb.endRenderPass();
        cb.end();

        present(i);
    }
};

int main() {
    try {
        texture text{};

        text.run();

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
    }

    return 0;
}
