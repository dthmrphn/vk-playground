#include "application.hpp"

#include <fmt/core.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <compute.comp.hpp>
#include <compute.frag.hpp>
#include <compute.vert.hpp>

struct vertex {
    glm::vec2 pos;
    glm::vec3 color;
    glm::vec2 coord;

    static constexpr vk::VertexInputBindingDescription binding_desc() {
        return {0, sizeof(vertex), vk::VertexInputRate::eVertex};
    }

    static constexpr std::array<vk::VertexInputAttributeDescription, 3> attribute_desc() {
        return {
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat, offsetof(vertex, pos)},
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

struct compute : public common::application<compute> {
    static constexpr auto local_size = 32;

    vk::raii::Pipeline _pipeline{nullptr};
    vk::raii::PipelineLayout _pipeline_layout{nullptr};

    vulkan::device_buffer _verticies_buffer;
    vulkan::device_buffer _indices_buffer;
    vulkan::host_buffer _uniform_buffer;
    vulkan::texture _input_texture;
    vulkan::texture _output_texture;

    vk::raii::DescriptorSetLayout _descriptor_layout{nullptr};
    vk::raii::DescriptorPool _descriptor_pool{nullptr};
    vk::raii::DescriptorSet _descriptor_set{nullptr};

    vk::raii::Semaphore _graphic_semaphore{nullptr};

    struct {
        vk::Queue queue{nullptr};
        vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
        vk::raii::DescriptorSet descriptor_set{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::Semaphore semaphore{nullptr};
        vk::raii::CommandPool command_pool{nullptr};
        vk::raii::CommandBuffer command_buffer{nullptr};
    } _compute;

    compute() : common::application<compute>({"compute", 1, "engine", 1, VK_API_VERSION_1_0}, 800, 600) {
        make_vertex_buffer();
        make_indices_buffer();
        make_input_image();

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
            {vk::DescriptorType::eStorageImage, 2},
        };

        vk::DescriptorPoolCreateInfo dpci{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, sizes};
        _descriptor_pool = _device.make_descriptor_pool(dpci);

        vk::DescriptorSetAllocateInfo dsai{_descriptor_pool, *_descriptor_layout};
        _descriptor_set = std::move(_device.make_descriptor_sets(dsai).front());

        vk::DescriptorBufferInfo dbi{_uniform_buffer.buf(), 0, sizeof(uniform)};
        vk::DescriptorImageInfo dii{_output_texture.sampler(), _output_texture.view(), vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet wdss[] = {
            {_descriptor_set, 0, 0, vk::DescriptorType::eUniformBuffer, {}, dbi},
            {_descriptor_set, 1, 0, vk::DescriptorType::eCombinedImageSampler, dii},
        };
        _device.logical().updateDescriptorSets(wdss, nullptr);

        const auto vert_shader = _device.make_shader_module({{}, compute_vert::size, compute_vert::code});
        const auto frag_shader = _device.make_shader_module({{}, compute_frag::size, compute_frag::code});

        vk::PipelineShaderStageCreateInfo shader_stages[] = {
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vert_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, frag_shader, "main"},
        };

        constexpr auto binding_desc = vertex::binding_desc();
        constexpr auto attribute_desc = vertex::attribute_desc();
        vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, binding_desc, attribute_desc};
        vk::PipelineLayoutCreateInfo plci{{}, *_descriptor_layout};
        _pipeline_layout = _device.make_pipeline_layout(plci);

        vk::GraphicsPipelineCreateInfo pci = default_pipeline_info{};
        pci.setStages(shader_stages)
            .setPVertexInputState(&vertex_input_state)
            .setLayout(_pipeline_layout)
            .setRenderPass(_render_pass);
        _pipeline = _device.make_pipeline(pci);

        _graphic_semaphore = _device.make_semaphore({});
        vk::SubmitInfo si{{}, {}, {}, *_graphic_semaphore};
        _graphic_queue.submit(si);
        _graphic_queue.waitIdle();

        make_compute_context();
    }

    void make_vertex_buffer() {
        std::array<vertex, 4> verticies = {{
            {{-0.5, +0.5}, {1.0, 0.0, 0.0}, {0.0, 1.0}},
            {{+0.5, +0.5}, {0.0, 1.0, 0.0}, {1.0, 1.0}},
            {{+0.5, -0.5}, {0.0, 0.0, 1.0}, {1.0, 0.0}},
            {{-0.5, -0.5}, {0.5, 0.5, 0.5}, {0.0, 0.0}},
        }};

        constexpr auto size = sizeof(vertex) * verticies.size();
        vulkan::host_buffer staging{_device, size, vk::BufferUsageFlagBits::eTransferSrc};
        staging.copy(verticies.data(), size);

        _verticies_buffer = {
            _device,
            size,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        };

        vk::ClearValue clear_value{vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f}};
        _device.copy_buffers(staging.buf(), _verticies_buffer.buf(), size);
    }

    void make_indices_buffer() {
        std::array<std::uint32_t, 6> indicies = {0, 1, 2, 2, 3, 0};

        constexpr auto size = sizeof(std::uint32_t) * indicies.size();
        vulkan::host_buffer staging{_device, size, vk::BufferUsageFlagBits::eTransferSrc};
        staging.copy(indicies.data(), size);

        _indices_buffer = {
            _device,
            size,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        };

        _device.copy_buffers(staging.buf(), _indices_buffer.buf(), size);
    }

    void make_input_image() {
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
        };
        staging.copy(data, size);

        _input_texture = {
            _device,
            width,
            height,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
        };

        _device.copy_buffer_to_image(staging.buf(), _input_texture.image(), _input_texture.extent());

        _device.image_transition(_input_texture.image(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral);

        _output_texture = {
            _device,
            width,
            height,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
        };

        _device.image_transition(_output_texture.image(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
    }

    void make_compute_context() {
        _compute.queue = _device.compute_queue();
        vk::DescriptorSetLayoutBinding bindings[] = {
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        };

        vk::DescriptorSetLayoutCreateInfo dslci{{}, bindings};
        _compute.descriptor_layout = _device.make_descriptor_set_layout(dslci);

        vk::PipelineLayoutCreateInfo plci{{}, *_compute.descriptor_layout};
        _compute.pipeline_layout = _device.make_pipeline_layout(plci);

        vk::DescriptorSetAllocateInfo dsai{_descriptor_pool, *_compute.descriptor_layout};
        _compute.descriptor_set = std::move(_device.make_descriptor_sets(dsai).front());

        vk::DescriptorImageInfo input_dii{_input_texture.sampler(), _input_texture.view(), vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo output_dii{_output_texture.sampler(), _output_texture.view(), vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet wds[] = {
            vk::WriteDescriptorSet{_compute.descriptor_set, 0, 0, vk::DescriptorType::eStorageImage, input_dii},
            vk::WriteDescriptorSet{_compute.descriptor_set, 1, 0, vk::DescriptorType::eStorageImage, output_dii},
        };

        _device.logical().updateDescriptorSets(wds, nullptr);

        const auto comp_shader = _device.make_shader_module({{}, compute_comp::size, compute_comp::code});
        vk::PipelineShaderStageCreateInfo pssci{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eCompute, comp_shader, "main"},
        };
        vk::ComputePipelineCreateInfo cpci{{}, pssci, _compute.pipeline_layout};
        _compute.pipeline = _device.make_pipeline(cpci);

        _compute.command_pool = _device.make_command_pool({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, _graphic_queue_index});
        vk::CommandBufferAllocateInfo cbai{_compute.command_pool, vk::CommandBufferLevel::ePrimary, 1};
        _compute.command_buffer = std::move(_device.make_command_buffers(cbai).front());

        _compute.semaphore = _device.make_semaphore({});
    }

    void record_compute() {
        _compute.queue.waitIdle();

        _compute.command_buffer.begin({});
        _compute.command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, _compute.pipeline);
        _compute.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, _compute.pipeline_layout, 0, *_compute.descriptor_set, nullptr);
        _compute.command_buffer.dispatch(_input_texture.extent().width / local_size, _input_texture.extent().height / local_size, 1);
        _compute.command_buffer.end();

        vk::PipelineStageFlags wait_flags{vk::PipelineStageFlagBits::eComputeShader};

        vk::SubmitInfo info{
            *_graphic_semaphore,
            wait_flags,
            *_compute.command_buffer,
            *_compute.semaphore,
        };
        _compute.queue.submit(info);
    }

    void present(std::uint32_t index) {
        const auto& [cb, image_available_semaphore, render_finished_semaphore, fence] = _frames[_current_frame];
        vk::PipelineStageFlags wait_flags[] = {vk::PipelineStageFlagBits::eVertexInput, vk::PipelineStageFlagBits::eColorAttachmentOutput};
        vk::Semaphore wait_semaphores[] = {_compute.semaphore, image_available_semaphore};
        vk::Semaphore signal_semaphores[] = {_graphic_semaphore, render_finished_semaphore};
        vk::SubmitInfo submit{
            wait_semaphores,
            wait_flags,
            *cb,
            signal_semaphores,
        };
        _graphic_queue.submit(submit, fence);

        vk::PresentInfoKHR present_info{*render_finished_semaphore, *_swapchain.get(), index};
        auto rv = _present_queue.presentKHR(present_info);
        if (rv != vk::Result::eSuccess) {
            fmt::print("present err: {}\n", vk::to_string(rv));
        }

        _current_frame = (_current_frame + 1) % frames_in_flight;
    }

    void record(std::uint32_t i) {
        record_compute();

        const auto& cb = _frames[_current_frame].command_buffer;

        const float time = glfwGetTime();
        uniform ubo{
            glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::mat4(1.0f),
            glm::mat4(1.0f),
        };
        _uniform_buffer.copy(&ubo, sizeof(ubo));

        vk::ClearValue clear_values[] = {
            vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f},
            vk::ClearDepthStencilValue{1.0f, 0},
        };
        vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[i], {{0, 0}, _swapchain.extent()}, clear_values};
        vk::Viewport viewport{0.0f, 0.0f, (float)_swapchain.extent().width, (float)_swapchain.extent().height, 0.0f, 1.0f};

        cb.reset();
        cb.begin({});
        cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _pipeline_layout, 0, {_descriptor_set}, nullptr);
        cb.bindVertexBuffers(0, *_verticies_buffer.buf(), {0});
        cb.bindIndexBuffer(_indices_buffer.buf(), 0, vk::IndexType::eUint32);
        cb.setViewport(0, viewport);
        cb.setScissor(0, vk::Rect2D{{0, 0}, _swapchain.extent()});
        cb.drawIndexed(6, 1, 0, 0, 0);
        cb.endRenderPass();
        cb.end();
    }
};

int main() {
    try {
        compute text{};

        text.run();

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
    }

    return 0;
}
