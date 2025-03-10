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
        _init_vk_instance();
        _init_surface();
        _pick_physical_device();
        _init_logical_device();
    }

    void
    _init_vk_instance() noexcept;

    void
    _init_surface() noexcept;

    void
    _pick_physical_device() noexcept;

    void
    _init_logical_device() noexcept;

    bool
    _check_validation_layer_support(std::span<const char*> validation_layers
    ) const noexcept;

private:
#if defined(NDEBUG) || !defined(USE_VALIDATION_LAYERS)
    static constexpr bool s_enable_validation_layers {false};
#else
    static constexpr bool s_enable_validation_layers {true};
#endif

    GLFWwindow* _window {nullptr};
    vk::UniqueInstance _vk_instance {nullptr};
    vk::UniqueSurfaceKHR _surface {nullptr};
    // PhysicalDevice is implicitaly destroyed when _vk_instance is destroyed.
    vk::PhysicalDevice _physical_device {nullptr};
    vk::UniqueDevice _device {nullptr};
    // Device is implicitaly destroyed when _devie is destroyed.
    vk::Queue _graphics_queue {nullptr};
    vk::Queue _present_queue {nullptr};
};

} // namespace core
