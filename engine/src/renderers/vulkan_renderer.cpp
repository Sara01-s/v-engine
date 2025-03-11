#include <core/renderers/vulkan_renderer.hpp>

#include <algorithm> // clamp.
#include <array>
#include <cstring> // strcmp.
#include <limits>
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
_check_validation_layer_support(
    std::span<char const*> const requested_validation_layers
) noexcept {
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
) noexcept {
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

    return info;
}

vk::SurfaceFormatKHR
_choose_swap_surface_format(
    std::vector<vk::SurfaceFormatKHR> const& available_formats
) noexcept {
    // If this fails, we could rank the formats and return the best one.
    for (auto const& available_format : available_formats) {
        Log::info(
            "Checking available format: ",
            vk::to_string(available_format.format),
            ", ColorSpace: ",
            vk::to_string(available_format.colorSpace)
        );

        if (available_format.format == vk::Format::eB8G8R8A8Srgb &&
            available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return available_format;
        }
    }

    return available_formats[0];
}

vk::PresentModeKHR
_choose_swap_present_mode(
    std::vector<vk::PresentModeKHR> const& available_present_modes
) noexcept {
    //
    for (auto const& present_mode : available_present_modes) {
        // Recommended default.
        // src: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html#_enabling_device_extensions:~:text=personally%20think%20that-,VK_PRESENT_MODE_MAILBOX_KHR,-is%20a%20very
        if (present_mode == vk::PresentModeKHR::eMailbox) {
            return present_mode;
        }
    }

    return vk::PresentModeKHR::eFifo; // Guaranteed to be available.
    // src: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html#_enabling_device_extensions:~:text=VK_PRESENT_MODE_FIFO_KHR%20mode%20is%20guaranteed%20to%20be%20available
}

vk::Extent2D
_choose_swap_extent(
    vk::SurfaceCapabilitiesKHR const& capabilities,
    GLFWwindow* const window
) noexcept {
    // Swap extent is the resolution of the swap chain images (in px).
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        return capabilities.currentExtent;
    }

    int width {}, height {};
    glfwGetFramebufferSize(window, &width, &height);

    // Convert to u32.
    vk::Extent2D actual_extent = {
        static_cast<u32>(width),
        static_cast<u32>(height)
    };

    u32 clamped_width = std::clamp(
        actual_extent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );

    u32 clamped_height = std::clamp(
        actual_extent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    actual_extent.setWidth(clamped_width);
    actual_extent.setHeight(clamped_height);

    return actual_extent;
}

void
VulkanRenderer::_init_swap_chain() noexcept {
    Log::header("Initializing Swap Chain.");

    SwapChainSupportInfo swap_chain_info =
        _query_swapchain_support(_physical_device, _surface);

    vk::SurfaceFormatKHR surface_format =
        _choose_swap_surface_format(swap_chain_info.formats);
    Log::info("Chosen surface format: ", vk::to_string(surface_format.format));

    vk::PresentModeKHR present_mode =
        _choose_swap_present_mode(swap_chain_info.present_modes);
    Log::info("Chosen present mode: ", vk::to_string(present_mode));

    vk::Extent2D extent =
        _choose_swap_extent(swap_chain_info.capabilities, _window);
    Log::info("Chosen swap extent: ", extent.width, "x", extent.height);

    u32 image_count = swap_chain_info.capabilities.minImageCount + 1;
    u32 const max_image_count = swap_chain_info.capabilities.maxImageCount;

    if (max_image_count > 0 && image_count > max_image_count) {
        image_count = max_image_count;
    }
    Log::info("Image count: ", image_count);

    vk::SwapchainCreateInfoKHR swap_chain_create_info {
        .surface = *_surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment
    };

    QueueFamilyIndices indices =
        _find_queue_families(_physical_device, _surface);
    u32 const queue_family_indices[] = {
        indices.graphics_family.value(),
        indices.present_family.value()
    };

    if (indices.graphics_family != indices.present_family) {
        swap_chain_create_info.imageSharingMode = vk::SharingMode::eConcurrent;
        swap_chain_create_info.queueFamilyIndexCount = 2;
        swap_chain_create_info.pQueueFamilyIndices = queue_family_indices;
        Log::info("Using concurrent sharing mode for swap chain images.");
    } else {
        swap_chain_create_info.imageSharingMode = vk::SharingMode::eExclusive;
        swap_chain_create_info.queueFamilyIndexCount = 0;
        swap_chain_create_info.pQueueFamilyIndices = nullptr;
        Log::info("Using exclusive sharing mode for swap chain images.");
    }

    swap_chain_create_info.preTransform =
        swap_chain_info.capabilities.currentTransform;
    swap_chain_create_info.compositeAlpha =
        vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swap_chain_create_info.presentMode = present_mode;
    swap_chain_create_info.clipped = vk::True;
    swap_chain_create_info.oldSwapchain = nullptr;

    core_assert(
        _logical_device.get(),
        "Device is nullptr, please initialize it."
    );
    auto [result, swap_chain] =
        _logical_device.get().createSwapchainKHRUnique(swap_chain_create_info);

    core_assert(result == vk::Result::eSuccess, "Failed to create Swap Chain");
    _swap_chain = std::move(swap_chain);

    Log::info(Log::LIGHT_GREEN, "Swap Chain successfully created.");

    // Retrieve images.
    auto [result2, images] =
        _logical_device->getSwapchainImagesKHR(*_swap_chain);

    core_assert(
        result2 == vk::Result::eSuccess,
        "Failed to retrieve swap chain images."
    );

    Log::info("Retrieved (", images.size(), ") swap chain images.");

    _swap_chain_images = std::move(images);

    // Store swap chain state.
    _swap_chain_image_format = surface_format.format;
    _swap_chain_extent = extent;
}

#pragma endregion SWAP_CHAIN

#pragma region IMAGE_VIEWS

void
VulkanRenderer::_init_image_views() noexcept {
    _image_views.resize(_swap_chain_images.size());

    for (usize i {}; i < _swap_chain_images.size(); ++i) {
        vk::ImageViewCreateInfo image_view_create_info {
            .image = _swap_chain_images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = _swap_chain_image_format,
        };

        // Use images as color targets, without mipmaps and multiple layers.
        image_view_create_info.subresourceRange.aspectMask =
            vk::ImageAspectFlagBits::eColor;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;

        // Default color components mapping.
        image_view_create_info.components.r = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.g = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.b = vk::ComponentSwizzle::eIdentity;
        image_view_create_info.components.a = vk::ComponentSwizzle::eIdentity;

        auto [result, image_view] =
            _logical_device->createImageViewUnique(image_view_create_info);

        core_assert(
            result == vk::Result::eSuccess,
            "Failed to create unique image view."
        );

        _image_views[i] = std::move(image_view);
    }
}

#pragma endregion IMAGE_VIEWS

#pragma region PHYSICAL_DEVICE

bool
_check_device_extension_support(vk::PhysicalDevice const& device) {
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

        swap_chain_adequate = !swap_chain_info.formats.empty() &&
            !swap_chain_info.present_modes.empty();
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
VulkanRenderer::_init_physical_device() noexcept {
    core_assert(_surface.get(), "Surface is nullptr.");

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

    _logical_device = std::move(device);
    Log::info(Log::LIGHT_GREEN, "Logical Device successfully created.");

    // Create queues.
    _graphics_queue = _logical_device->getQueue(
        queue_family_indices.graphics_family.value(),
        0x0
    );
    Log::sub_info("Graphics queue created: ", _graphics_queue);

    _present_queue = _logical_device->getQueue(
        queue_family_indices.present_family.value(),
        0x0
    );
    Log::sub_info("Present queue created: ", _present_queue);
}

#pragma endregion LOGICAL_DEVICE

} // namespace core
