#include "vulkan.hpp"

#include <chrono>
#include <fmt/core.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <headless.comp.hpp>

constexpr static const char* layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* extensions[] = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

constexpr static vk::ApplicationInfo app_info = {
    "headless",
    1,
    "engine",
    1,
    VK_API_VERSION_1_0,
};

struct headless {
    static constexpr auto local_size = 32;

    vulkan::device _device;
    vulkan::texture _input_texture;
    vulkan::texture _output_texture;
    vulkan::host_buffer _staging;

    vk::DeviceSize _buffer_size;

    vk::raii::DescriptorPool _descriptor_pool{nullptr};

    struct {
        vk::Queue queue{nullptr};
        std::uint32_t queue_index{};
        vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
        vk::raii::DescriptorSet descriptor_set{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::CommandPool command_pool{nullptr};
        vk::raii::CommandBuffer command_buffer{nullptr};
        vk::raii::Fence fence{nullptr};
    } _compute;

    headless(std::uint32_t width, std::uint32_t height) {
        _device = vulkan::device{
            app_info,
            layers,
            nullptr,
            extensions,
            vk::QueueFlagBits::eCompute,
            true,
        };

        vk::DescriptorPoolSize sizes[] = {
            {vk::DescriptorType::eStorageImage, 2},
        };

        vk::DescriptorPoolCreateInfo dpci{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, sizes};
        _descriptor_pool = _device.make_descriptor_pool(dpci);

        _compute.queue = _device.compute_queue();
        _compute.queue_index = _device.queue_family_index(vk::QueueFlagBits::eCompute);

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

        const auto comp_shader = _device.make_shader_module({{}, headless_comp::size, headless_comp::code});
        vk::PipelineShaderStageCreateInfo pssci{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eCompute, comp_shader, "main"},
        };
        vk::ComputePipelineCreateInfo cpci{{}, pssci, _compute.pipeline_layout};
        _compute.pipeline = _device.make_pipeline(cpci);

        _compute.command_pool = _device.make_command_pool({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, _compute.queue_index});
        vk::CommandBufferAllocateInfo cbai{_compute.command_pool, vk::CommandBufferLevel::ePrimary, 1};
        _compute.command_buffer = std::move(_device.make_command_buffers(cbai).front());

        _compute.fence = _device.make_fence({vk::FenceCreateFlagBits::eSignaled});

        resize(width, height);
    }

    void resize(std::uint32_t width, std::uint32_t height) {
        const vk::DeviceSize dev_size = width * height * 4;
        _staging = {
            _device,
            dev_size,
            vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
        };

        _input_texture = {
            _device,
            width,
            height,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
        };
        _device.image_transition(_input_texture.image(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

        _output_texture = {
            _device,
            width,
            height,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
        };
        _device.image_transition(_output_texture.image(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

        vk::DescriptorImageInfo input_dii{_input_texture.sampler(), _input_texture.view(), vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo output_dii{_output_texture.sampler(), _output_texture.view(), vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet wds[] = {
            vk::WriteDescriptorSet{_compute.descriptor_set, 0, 0, vk::DescriptorType::eStorageImage, input_dii},
            vk::WriteDescriptorSet{_compute.descriptor_set, 1, 0, vk::DescriptorType::eStorageImage, output_dii},
        };

        _device.logical().updateDescriptorSets(wds, nullptr);
    }

    void process_image(const void* src, void* dst, std::int32_t w, std::int32_t h, std::int32_t channels = 4) {
        const vk::DeviceSize dev_size = w * h * 4;
        _staging.copy(src, dev_size);

        while (vk::Result::eTimeout == _device.logical().waitForFences(*_compute.fence, vk::True, -1)) {
        }
        _device.logical().resetFences(*_compute.fence);

        _compute.command_buffer.begin({});
        vulkan::utils::copy_buffer_to_image(*_compute.command_buffer, _staging.buf(), _input_texture.image(), _input_texture.extent(), vk::ImageLayout::eGeneral);
        _compute.command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, _compute.pipeline);
        _compute.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, _compute.pipeline_layout, 0, *_compute.descriptor_set, nullptr);
        _compute.command_buffer.dispatch(_input_texture.extent().width / local_size, _input_texture.extent().height / local_size, 1);

        vk::BufferImageCopy bic{
            0,
            0,
            0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            _output_texture.extent(),
        };
        _compute.command_buffer.copyImageToBuffer(_output_texture.image(), vk::ImageLayout::eGeneral, _staging.buf(), bic);
        _compute.command_buffer.end();

        vk::SubmitInfo info{
            nullptr,
            {},
            *_compute.command_buffer,
        };
        _compute.queue.submit(info, _compute.fence);

        wait_idle();

        _staging.copy_to(dst, dev_size);
    }

    void wait_idle() {
        _device.logical().waitIdle();
    }
};

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::runtime_error("usage: headless /path/to/image");
        }

        int w{}, h{}, c{}, wc{4};
        auto data = stbi_load(argv[1], &w, &h, &c, wc);
        if (!data) {
            throw std::runtime_error("failed to load image");
        }

        std::uint32_t width = w;
        std::uint32_t height = h;
        headless headless{width, height};
        headless.resize(width, height);

        std::vector<std::uint8_t> image_bytes(w * h * wc);

        for (int i = 0; i < 20; ++i) {
            const auto now = std::chrono::steady_clock::now();
            headless.process_image(data, image_bytes.data(), w, h);
            const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now).count();
            fmt::print("took {}ms\n", dur);
        }

        stbi_write_jpg("headless.jpg", w, h, 4, image_bytes.data(), 90);

        headless.wait_idle();

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
    }

    return 0;
}
