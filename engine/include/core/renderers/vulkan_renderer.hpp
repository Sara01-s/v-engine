#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS // Use designated initializers.
#define VULKAN_HPP_NO_EXCEPTIONS // Use result pattern instead.
#include <vulkan/vulkan.hpp> // Must be included before GLFW.

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <optional>

#include "../log.hpp"
#include "../types.hpp"

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

private:
    void
    _init_vulkan() noexcept {
        // Execution order is extremely important, do not modify.
        _init_vk_instance();
        _init_surface();
        _init_physical_device();
        _init_logical_device();
        _init_swap_chain();
        _init_image_views();
    }

    void
    _init_vk_instance() noexcept;

    void
    _init_surface() noexcept;

    void
    _init_physical_device() noexcept;

    void
    _init_logical_device() noexcept;

    void
    _init_swap_chain() noexcept;

    void
    _init_image_views() noexcept;

private:
#if defined(NDEBUG) || !defined(USE_VALIDATION_LAYERS)
    static constexpr bool s_enable_validation_layers {false};
#else
    static constexpr bool s_enable_validation_layers {true};
#endif

    // Vulkan Core.
    GLFWwindow* _window {nullptr};
    vk::UniqueInstance _vk_instance {nullptr};
    vk::UniqueSurfaceKHR _surface {nullptr};
    // PhysicalDevice is implicitaly destroyed when _vk_instance is destroyed.
    vk::PhysicalDevice _physical_device {nullptr};
    vk::UniqueDevice _logical_device {nullptr};
    // Device is implicitaly destroyed when _devie is destroyed.
    vk::Queue _graphics_queue {nullptr};
    vk::Queue _present_queue {nullptr};

    // Swap chain.
    vk::UniqueSwapchainKHR _swap_chain {nullptr};
    std::vector<vk::Image> _swap_chain_images {};
    vk::Format _swap_chain_format {};
    vk::Extent2D _swap_chain_extent {};

    // Image views.
    std::vector<vk::ImageView> _image_views {};
};

} // namespace core
