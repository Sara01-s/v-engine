#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS // Use designated initializers.
#define VULKAN_HPP_NO_EXCEPTIONS // Use result pattern instead.
#define VULKAN_HPP_ASSERT_ON_RESULT // We will use our own asserts.
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.hpp> // Must be included before GLFW.

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <fstream>
#include <optional>

#include "../log.hpp"
#include "../renderer.hpp"
#include "../stb_image.h"
#include "../tiny_obj_loader.hpp"
#include "../types.hpp"
#include "vulkan_drawable.hpp"

#define USE_VALIDATION_LAYERS

namespace core {

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() = default;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer&
    operator=(const VulkanRenderer&) = delete;
    VulkanRenderer&
    operator=(VulkanRenderer&&) = delete;

    void
    init(GLFWwindow* window, RenderInfo const& test_render_info) noexcept;

    void
    render() noexcept;

    void
    cleanup() noexcept;

    void
    set_resized(bool value) noexcept {
        _framebuffer_resized = value;
    }

private:
    void
    _init_vulkan() noexcept {
        core_assert(
            !_default_vertex_shader_spirv.empty() &&
                !_default_fragment_shader_spirv.empty(),
            "Please add a default material to initialize renderer."
        );

        // Execution order is extremely important, do not modify.
        _create_vk_instance();
        _create_surface();
        _create_physical_device();
        _create_logical_device();
        _create_swap_chain();
        _create_image_views();
        _create_render_pass();
        _create_descriptor_set_layout();
        _create_graphics_pipeline();
        _create_command_pool();
        _create_depth_resources();
        _create_framebuffers();
        _create_texture_image();
        _create_texture_image_view();
        _create_texture_sampler();
        _load_model();
        _create_vertex_buffer();
        _create_index_buffer();
        _create_uniform_buffers();
        _create_descriptor_pool();
        _create_descriptor_sets();
        _create_command_buffers();
        _create_sync_objects();
    }

    void
    _draw_frame() noexcept;

    void
    _create_vk_instance() noexcept;

    void
    _create_surface() noexcept;

    void
    _create_physical_device() noexcept;

    void
    _create_logical_device() noexcept;

    void
    _create_swap_chain() noexcept;

    void
    _cleanup_swap_chain() noexcept;

    void
    _recreate_swap_chain() noexcept;

    void
    _create_image_views() noexcept;

    void
    _create_render_pass() noexcept;

    void
    _create_descriptor_set_layout() noexcept;

    void
    _create_descriptor_pool() noexcept;

    void
    _create_descriptor_sets() noexcept;

    void
    _create_graphics_pipeline() noexcept;

    void
    _create_framebuffers() noexcept;

    void
    _create_command_pool() noexcept;

    void
    _create_command_buffers() noexcept;

    void
    _create_sync_objects() noexcept;

    void
    _create_vertex_buffer() noexcept;

    void
    _create_index_buffer() noexcept;

    void
    _create_uniform_buffers() noexcept;

    void
    _create_texture_image() noexcept;

    void
    _create_texture_image_view() noexcept;

    void
    _create_texture_sampler() noexcept;

    void
    _create_depth_resources() noexcept;

    void
    _load_model() noexcept;

    void
    _update_uniform_buffer(u32 current_image) noexcept;

    vk::UniqueImageView
    _create_image_view(
        vk::Image image,
        vk::Format format,
        vk::ImageAspectFlags aspect_flags
    ) noexcept;

    void
    _record_command_buffer(u32 image_index) noexcept;

    vk::CommandBuffer
    _begin_single_time_commands() noexcept;

    void
    _end_single_time_commands(vk::CommandBuffer& command_buffer) noexcept;

    void
    _create_buffer_unique(
        vk::DeviceSize const size,
        vk::BufferUsageFlags const usage,
        vk::MemoryPropertyFlags const properties,
        vk::UniqueBuffer& buffer,
        vk::UniqueDeviceMemory& buffer_memory
    ) noexcept;

    void
    _copy_buffer(
        vk::UniqueBuffer& src_buffer,
        vk::UniqueBuffer& dst_buffer,
        vk::DeviceSize const size
    ) noexcept;

    std::vector<u32>
    _compile_shader_to_spirv(
        const std::string& source_code,
        std::string const& file_path,
        shaderc_shader_kind shader_kind
    ) noexcept;

    void
    _create_image(
        u32 width,
        u32 height,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::UniqueImage& image,
        vk::UniqueDeviceMemory& image_memory
    ) noexcept;

    void
    _copy_buffer_to_image(
        vk::UniqueBuffer& buffer,
        vk::UniqueImage& image,
        u32 width,
        u32 height
    ) noexcept;

    void
    _transition_image_layout(
        vk::Image image,
        vk::Format format,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout
    ) noexcept;

    vk::Format
    _find_depth_format(vk::PhysicalDevice const& physical_device) noexcept;

private:
#if defined(NDEBUG) || !defined(USE_VALIDATION_LAYERS)
    static constexpr bool s_enable_validation_layers {false};
#else
    static constexpr bool s_enable_validation_layers {true};
#endif
    static constexpr u32 s_max_frames_in_flight {2U};

    template <typename T>
    using PerFrameArray = std::array<T, s_max_frames_in_flight>;

    u32 _current_frame {0};
    bool _framebuffer_resized {false};

    // Vulkan Core.
    GLFWwindow* _window {nullptr};
    vk::UniqueInstance _vk_instance {nullptr};
    vk::UniqueSurfaceKHR _surface {nullptr};
    // PhysicalDevice is implicitaly destroyed when _vk_instance is destroyed.
    vk::PhysicalDevice _physical_device {nullptr};
    vk::UniqueDevice _device {nullptr}; // LOGICAL device.
    vk::Queue _graphics_queue {nullptr};
    vk::Queue _present_queue {nullptr};

    // Swap chain.
    vk::UniqueSwapchainKHR _swap_chain {nullptr};
    std::vector<vk::Image> _swap_chain_images {};
    vk::Format _swap_chain_image_format {};
    vk::Extent2D _swap_chain_extent {};

    // Image views.
    std::vector<vk::UniqueImageView> _swap_chain_image_views {};

    // Render Pipeline.
    vk::UniqueRenderPass _render_pass {nullptr};
    vk::UniqueDescriptorSetLayout _descriptor_set_layout {nullptr};
    vk::UniqueDescriptorPool _descriptor_pool {nullptr};
    PerFrameArray<vk::UniqueDescriptorSet> _descriptor_sets {};
    vk::UniquePipelineLayout _pipeline_Layout {nullptr};
    vk::UniquePipeline _graphics_pipeline {nullptr};
    std::vector<u32> _default_vertex_shader_spirv {};
    std::vector<u32> _default_fragment_shader_spirv {};

    // Framebuffers.
    std::vector<vk::UniqueFramebuffer> _swap_chain_framebuffers {};

    // Commands.
    vk::UniqueCommandPool _command_pool {nullptr};
    PerFrameArray<vk::UniqueCommandBuffer> _command_buffers {};

    // Sync objetcs.
    // An image has been acquired from the swapchain and is ready for redering.
    PerFrameArray<vk::UniqueSemaphore> _image_available_semaphores {};
    // Rendering has finished and presentation can happen.
    PerFrameArray<vk::UniqueSemaphore> _render_finished_semapahores {};
    // Indicates if a frames is currentyl rendering.
    // (to make sure only one frame is rendering at a time).
    PerFrameArray<vk::UniqueFence> _frame_in_flight_fences {};

    // Buffers.
    vk::UniqueBuffer _vertex_buffer {nullptr};
    vk::UniqueDeviceMemory _vertex_buffer_memory {nullptr};
    vk::UniqueBuffer _index_buffer {nullptr};
    vk::UniqueDeviceMemory _index_buffer_memory {nullptr};
    PerFrameArray<vk::UniqueBuffer> _uniform_buffers {};
    PerFrameArray<vk::UniqueDeviceMemory> _uniform_buffers_memory {};
    PerFrameArray<void*> _uniform_buffers_mapped {};

    // Data to draw.
    std::vector<Vertex> _vertices {};
    std::vector<u32> _indices {};

    // Textures.
    std::string _default_texture_path {};
    vk::UniqueImage _texture_image {nullptr};
    vk::UniqueDeviceMemory _texture_image_memory {nullptr};
    vk::UniqueImageView _texture_image_view {nullptr};
    vk::UniqueSampler _texture_sampler {nullptr};

    // Depth Buffering.
    vk::UniqueImage _depth_image {nullptr};
    vk::UniqueDeviceMemory _depth_image_memory {nullptr};
    vk::UniqueImageView _depth_image_view {nullptr};

    // Model.
    std::string _model_file_path {};
};

} // namespace core
