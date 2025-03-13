#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS // Use designated initializers.
#define VULKAN_HPP_NO_EXCEPTIONS // Use result pattern instead.
#include <vulkan/vulkan.hpp> // Must be included before GLFW.

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <fstream>
#include <optional>

#include "../log.hpp"
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
    init(GLFWwindow* window) noexcept;

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
        // Execution order is extremely important, do not modify.
        _create_vk_instance();
        _create_surface();
        _create_physical_device();
        _create_logical_device();
        _create_swap_chain();
        _create_image_views();
        _create_render_pass();
        _create_graphics_pipeline();
        _create_framebuffers();
        _create_command_pool();
        _create_vertex_buffer();
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
    _record_command_buffer(u32 const image_index) noexcept;

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

private:
#if defined(NDEBUG) || !defined(USE_VALIDATION_LAYERS)
    static constexpr bool s_enable_validation_layers {false};
#else
    static constexpr bool s_enable_validation_layers {true};
#endif
    static constexpr u32 s_max_frames_in_flight {2};

    u32 _current_frame {0};
    bool _framebuffer_resized {false};

    // Vulkan Core.
    GLFWwindow* _window {nullptr};
    vk::UniqueInstance _vk_instance {nullptr};
    vk::UniqueSurfaceKHR _surface {nullptr};
    // PhysicalDevice is implicitaly destroyed when _vk_instance is destroyed.
    vk::PhysicalDevice _physical_device {nullptr};
    vk::UniqueDevice _device {nullptr}; // LOGICAL device.
    // Device is implicitaly destroyed when _devie is destroyed.
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
    vk::UniquePipelineLayout _pipeline_Layout {nullptr};
    vk::UniquePipeline _graphics_pipeline {nullptr};

    // Framebuffers.
    std::vector<vk::UniqueFramebuffer> _swap_chain_framebuffers {};

    // Commands.
    vk::UniqueCommandPool _command_pool {nullptr};
    std::array<vk::UniqueCommandBuffer, s_max_frames_in_flight>
        _command_buffers {};

    // Sync objetcs.
    // An image has been acquired from the swapchain and is ready for redering.
    std::array<vk::UniqueSemaphore, s_max_frames_in_flight>
        _image_available_semaphores {};
    // Rendering has finished and presentation can happen.
    std::array<vk::UniqueSemaphore, s_max_frames_in_flight>
        _render_finished_semapahores {};
    // Indicates if a frames is currentyl rendering.
    // (to make sure only one frame is rendering at a time).
    std::array<vk::UniqueFence, s_max_frames_in_flight>
        _frame_in_flight_fences {};

    // Drawables.
    vk::UniqueBuffer _vertex_buffer {nullptr};
    vk::UniqueDeviceMemory _vertex_buffer_memory {nullptr};
    std::array<Vertex, 3> _vertices = {
        Vertex {.position = {0.0f, -0.5f}, .color = {1.0f, 0.0f, 0.5f}},
        Vertex {.position = {0.5f, 0.5f}, .color = {1.0f, 0.5f, 0.0f}},
        Vertex {.position = {-0.5, 0.5}, .color = {0.0f, 0.0f, 1.0f}},
    };
};

// Helper function to read shader files.
[[maybe_unused]] static std::vector<c8>
read_file(std::string const& file_name) noexcept {
    // Start reading file from the end and read it as binary data.
    std::ifstream file(file_name, std::ios::ate | std::ios::binary);

    v_assert(file.is_open(), "Failed to open file");

    // Since we started at the end, we can tell the file size :).
    usize const file_size = static_cast<usize>(file.tellg());
    std::vector<c8> buffer(file_size);

    file.seekg(0); // Move read pointer to beggining.
    file.read(buffer.data(), file_size); // Write data to buffer.
    file.close();

    return buffer;
}

} // namespace core
