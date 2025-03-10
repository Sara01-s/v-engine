#include <core/renderers/vulkan_renderer.hpp>

#include <array>
#include <cstring> // strcmp.
#include <map> // multipmap.
#include <set>
#include <span>

namespace core {
static constexpr std::array s_physical_device_extensions {
    vk::KHRSwapchainExtensionName
};

#pragma region PUBLIC_GFX_API

void
VulkanRenderer::init(GLFWwindow* window) noexcept {
    _window = window;

    Log::header("Initializing Vulkan Renderer.");
    _init_vulkan();
}

void
VulkanRenderer::cleanup() noexcept {
    glfwDestroyWindow(_window);
    glfwTerminate();
    Log::info("Vulkan cleanup completed.");
}

void
VulkanRenderer::render() noexcept {
    Log::header("Starting render loop.");
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();
    }
    Log::info("Render loop terminated.");
}

#pragma endregion

#pragma region VULKAN_INSTANCE

bool
VulkanRenderer::_check_validation_layer_support(
    std::span<char const*> const requested_validation_layers
) const noexcept {
    u32 layer_count {};

    vk::Result enumeration_result =
        vk::enumerateInstanceLayerProperties(&layer_count, nullptr);

    core_assert(
        enumeration_result == vk::Result::eSuccess,
        "Failed to enumerate instance layer properties count."
    );

    std::vector<vk::LayerProperties> available_layers(layer_count);

    vk::Result initialization_result = vk::enumerateInstanceLayerProperties(
        &layer_count,
        available_layers.data()
    );

    core_assert(
        initialization_result == vk::Result::eSuccess,
        "Failed to initialize instance layer properties."
    );

    Log::info("Available Validation Layers:");
    for (char const* layer_name : requested_validation_layers) {
        bool layer_found = false;

        for (auto const& layer_properties : available_layers) {
            Log::sub_info(layer_properties.layerName);
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layer_found = true;
                break;
            }
        }

        if (!layer_found) {
            Log::warn("Requested but not found: ");
            Log::sub_warn(layer_name);
            return false;
        }
    }

    return true;
}

void
VulkanRenderer::_init_vk_instance() noexcept {
    u32 version {0};
    vkEnumerateInstanceVersion(&version);

    Log::info(
        "System can support vulkan variant: ",
        VK_API_VERSION_VARIANT(version)
    );

    Log::sub_info("Major: ", VK_API_VERSION_MAJOR(version));
    Log::sub_info("Minor: ", VK_API_VERSION_MINOR(version));
    Log::sub_info("Patch: ", VK_API_VERSION_PATCH(version));

    // Zero out patch number. (see VK_API_VERSION_PATCH definition).
    // This is done to ensure maximum compatibility / stability.
    version &= ~(0xFFFU); // Lower twelve bytes.

    vk::ApplicationInfo app_info {
        .pApplicationName = "App",
        .applicationVersion = 1,
        .pEngineName = "V-Engine",
        .engineVersion = 1,
        .apiVersion = version,
    };

    // GLFW Extensions.
    u32 glfw_extension_count {0};
    char const** glfw_extensions {
        glfwGetRequiredInstanceExtensions(&glfw_extension_count)
    };

    Log::info("GLFW Required extensions:");
    for (u32 i {}; i < glfw_extension_count; ++i) {
        Log::sub_info(glfw_extensions[i]);
    }

    vk::InstanceCreateInfo instance_create_info {
        .flags = vk::InstanceCreateFlags {},
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = glfw_extension_count,
        .ppEnabledExtensionNames = glfw_extensions,
    };

    if constexpr (s_enable_validation_layers) {
        std::array requested_layers = {"VK_LAYER_KHRONOS_validation"};

        core_assert(
            _check_validation_layer_support(requested_layers),
            "Validation layers requested but not available, please install them."
        );

        instance_create_info.enabledLayerCount =
            static_cast<u32>(requested_layers.size());
        instance_create_info.ppEnabledLayerNames = requested_layers.data();
    }

    auto [result, instance] = vk::createInstanceUnique(instance_create_info);

    core_assert(
        result == vk::Result::eSuccess,
        "Failed to create VK Instance."
    );

    _vk_instance = std::move(instance);
    Log::info(Log::LIGHT_GREEN, "Vulkan Instance successfully created.");
}

#pragma endregion

#pragma region SURFACE

void
VulkanRenderer::_init_surface() noexcept {
    VkSurfaceKHR c_surface;
    core_assert(
        glfwCreateWindowSurface(
            _vk_instance.get(),
            _window,
            nullptr,
            &c_surface
        ) == VK_SUCCESS,
        "Failed to create window surface."
    );

    _surface = vk::UniqueSurfaceKHR(c_surface, _vk_instance.get());
}

#pragma endregion SURFACE

#pragma region QUEUE_FAMILIES

struct QueueFamilyIndices {
    std::optional<u32> graphics_family {};
    std::optional<u32> present_family {};

    bool
    is_complete() const noexcept {
        // If you add a member to the struct, concatenate it with a "&& member.has_value()" here.
        return graphics_family.has_value() && present_family.has_value();
    }

    constexpr usize
    count() const noexcept {
        return 2;
    };
};

QueueFamilyIndices
_find_queue_families(
    vk::PhysicalDevice const& device,
    vk::UniqueSurfaceKHR const& surface
) noexcept {
    QueueFamilyIndices indices {};
    u32 i {};

    for (auto const& queue_family : device.getQueueFamilyProperties()) {
        if (indices.is_complete()) {
            break;
        }

        if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics_family = i;
        }

        auto const& [result, supported] = device.getSurfaceSupportKHR(
            indices.graphics_family.value(),
            surface.get()
        );

        core_assert(
            result == vk::Result::eSuccess,
            "Failed to check surface support"
        );

        if (supported) {
            indices.present_family = i;
        }

        ++i;
    }

    return indices;
}

#pragma endregion

#pragma region SWAP_CHAIN

struct SwapChainSupportInfo {
    vk::SurfaceCapabilitiesKHR capabilities {};
    std::vector<vk::SurfaceFormatKHR> formats {};
    std::vector<vk::PresentModeKHR> present_modes {};
};

SwapChainSupportInfo
_query_swapchain_support(
    vk::PhysicalDevice const& device,
    vk::UniqueSurfaceKHR const& surface
) {
    SwapChainSupportInfo info;

    Log::info(
        "Querying swapchain support for device: ",
        device.getProperties().deviceName
    );

    auto [result1, capabilities] = device.getSurfaceCapabilitiesKHR(*surface);
    core_assert(
        result1 == vk::Result::eSuccess,
        "Failed to get surface capabilities"
    );

    info.capabilities = capabilities;

    Log::sub_info("Surface capabilities retrieved.");

    auto [result2, formats] = device.getSurfaceFormatsKHR(*surface);
    core_assert(
        result2 == vk::Result::eSuccess,
        "Failed to get surface formats"
    );

    info.formats = std::move(formats);

    Log::sub_info(
        "Surface formats retrieved: ",
        info.formats.size(),
        " formats available."
    );

    for (const auto& format : info.formats) {
        Log::sub_info(
            (usize)2,
            "Format: ",
            vk::to_string(format.format),
            ", ColorSpace: ",
            vk::to_string(format.colorSpace)
        );
    }

    auto [result3, present_modes] = device.getSurfacePresentModesKHR(*surface);
    core_assert(result3 == vk::Result::eSuccess, "Failed to get present modes");

    info.present_modes = std::move(present_modes);

    Log::sub_info(
        "Present modes retrieved: ",
        info.present_modes.size(),
        " modes available."
    );

    for (const auto& present_mode : info.present_modes) {
        Log::sub_info((usize)2, "Present Mode: ", vk::to_string(present_mode));
    }

    return info;
}

#pragma endregion SWAP_CHAIN

#pragma region PHYSICAL_DEVICE

bool
_check_device_extension_support(vk::PhysicalDevice const& device) {
    u32 extension_count {0};
    auto [result, available_extensions] =
        device.enumerateDeviceExtensionProperties();

    core_assert(
        result == vk::Result::eSuccess,
        "Failed to get physical device extension properties."
    );

    Log::info("Available Device Extensions:");
    for (auto const& extension : available_extensions) {
        Log::sub_info(extension.extensionName);
    }

    std::set<std::string> required_extensions(
        s_physical_device_extensions.begin(),
        s_physical_device_extensions.end()
    );

    for (auto const& extension : available_extensions) {
        required_extensions.erase(extension.extensionName);
    }

    if (!required_extensions.empty()) {
        Log::warn("Missing required device extensions:");

        for (auto const& missing_extension : required_extensions) {
            Log::sub_warn(missing_extension);
        }
    }

    return required_extensions.empty();
}

bool
_is_device_suitable(
    vk::PhysicalDevice const& device,
    vk::UniqueSurfaceKHR const& surface
) noexcept {
    // Edit this function if you have exclusion criteria for devices.
    QueueFamilyIndices const& indices = _find_queue_families(device, surface);
    bool extensions_supported = _check_device_extension_support(device);
    bool swap_chain_adequate {false};

    if (extensions_supported) {
        SwapChainSupportInfo swap_chain_info =
            _query_swapchain_support(device, surface);

        swap_chain_adequate = !swap_chain_info.formats.empty()
            && !swap_chain_info.present_modes.empty();
    }

    bool suitable =
        indices.is_complete() && extensions_supported && swap_chain_adequate;

    Log::info(
        "Checking if device is suitable: ",
        device.getProperties().deviceName
    );
    Log::sub_info(
        "Extensions supported: ",
        Log::to_string(extensions_supported)
    );
    Log::sub_info(
        "Graphics family: ",
        Log::to_string(indices.graphics_family.has_value())
    );
    Log::sub_info(
        "Present family: ",
        Log::to_string(indices.present_family.has_value())
    );
    Log::sub_info("Suitable: ", Log::to_string(suitable));

    return suitable;
}

constexpr u32
_rate_device_suitability(vk::PhysicalDevice const& device) noexcept {
    // Edit this function to rate devices according to application needs.

    u32 score {0}; // Let's start the competition, will this device be the best?
    auto const& device_properties = device.getProperties();
    auto const& device_features = device.getFeatures();

    Log::info("Rating device: ", device_properties.deviceName);

    if (device_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1'000; // Ding ding ding! wow! very good GPU.
        Log::sub_info("Discrete GPU found, adding 1000 points.");
    }

    // Show us what you got Mr. Device.
    score += device_properties.limits.maxImageDimension2D;
    Log::sub_info(
        "Max image dimension 2D: ",
        device_properties.limits.maxImageDimension2D
    );
    Log::sub_info("Total score: ", score);

    return score;
}

void
VulkanRenderer::_pick_physical_device() noexcept {
    core_assert(_surface.get(), "Surface is nullptr.");

    u32 device_count {0};
    auto [result, physical_devices] = _vk_instance->enumeratePhysicalDevices();

    core_assert(
        result == vk::Result::eSuccess,
        "Failed to find GPUs with Vulkan support."
    );

    Log::info("Found (", physical_devices.size(), ") Physical devices:");
    for (auto const& device : physical_devices) {
        Log::sub_info(device.getProperties().deviceName);

        if (_is_device_suitable(device, _surface)) {
            _physical_device = device;
            break;
        }
    }

    core_assert(_physical_device, "Failed to find a suitable GPU.");

    std::multimap<u32, vk::PhysicalDevice> candidates {};

    for (auto const& device : physical_devices) {
        u32 const score = _rate_device_suitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    core_assert(candidates.rbegin()->first > 0, "Failed to rate GPUs.");

    _physical_device = candidates.rbegin()->second;
    Log::info(Log::LIGHT_GREEN, "Physical Device successfully selected.");
}

#pragma endregion PHYSICAL_DEVICE

#pragma region LOGICAL_DEVICE

void
VulkanRenderer::_init_logical_device() noexcept {
    QueueFamilyIndices const& queue_family_indices =
        _find_queue_families(_physical_device, _surface);

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos {};
    std::set<u32> const unique_queue_families = {
        queue_family_indices.graphics_family.value(),
        queue_family_indices.present_family.value(),
    };

    f32 queue_priority {1.0f};
    for (u32 const queue_family : unique_queue_families) {
        vk::DeviceQueueCreateInfo queue_create_info {
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };

        queue_create_infos.push_back(queue_create_info);
    }

    // Specify device features.
    vk::PhysicalDeviceFeatures device_features {}; // None.

    // Create Logical Device.
    vk::DeviceCreateInfo device_create_info {
        .queueCreateInfoCount = static_cast<u32>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        // Add per-device extensions here.
        .enabledExtensionCount =
            static_cast<u32>(s_physical_device_extensions.size()),
        .ppEnabledExtensionNames = s_physical_device_extensions.data(),
        .pEnabledFeatures = &device_features,
    };

    auto [result, device] =
        _physical_device.createDeviceUnique(device_create_info);

    core_assert(
        result == vk::Result::eSuccess,
        "Failed to create Logical Device"
    );

    _device = std::move(device);
    Log::info(Log::LIGHT_GREEN, "Logical Device successfully created.");

    // Create queues.
    _graphics_queue =
        _device->getQueue(queue_family_indices.graphics_family.value(), 0x0);
    Log::sub_info("Graphics queue created: ", _graphics_queue);

    _present_queue =
        _device->getQueue(queue_family_indices.present_family.value(), 0x0);
    Log::sub_info("Present queue created: ", _present_queue);
}

#pragma endregion LOGICAL_DEVICE

} // namespace core
