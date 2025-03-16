#include <core/renderers/vulkan_renderer.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm> // clamp.
#include <array>
#include <chrono>
#include <cstring> // strcmp.
#include <filesystem>
#include <fstream> // TODO - Delete
#include <limits>
#include <map> // multipmap.
#include <set>
#include <span>

namespace core {

static constexpr vk::Result s_success {vk::Result::eSuccess};
static constexpr std::array s_physical_device_extensions {
    vk::KHRSwapchainExtensionName
};

static void
s_frame_buffer_resize_callback(
    GLFWwindow* window,
    int new_width,
    int new_height
) {
    auto app =
        reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    app->set_resized(true);
}

// Functions to handle vk::Results more elegantly.
constexpr void
_vk_expect(vk::Result const& call_result, c8 const* message) noexcept {
    core_assert(call_result == vk::Result::eSuccess, message);
}

template <typename T>
constexpr T
_vk_expect(vk::ResultValue<T> result_value, const char* message) {
    if (result_value.result == vk::Result::eSuccess) {
        return std::move(result_value.value);
    }

    core_assert(false, message);
    return {};
}

#pragma region PUBLIC_GFX_API

void
VulkanRenderer::init(
    GLFWwindow* window,
    Material const& default_material
) noexcept {
    _window = window;
    _default_vertex_shader_spirv = _compile_shader_to_spirv(
        default_material.vertex_source,
        default_material.vertex_file_path,
        shaderc_shader_kind::shaderc_vertex_shader
    );

    _default_fragment_shader_spirv = _compile_shader_to_spirv(
        default_material.fragment_source,
        default_material.fragment_file_path,
        shaderc_shader_kind::shaderc_fragment_shader
    );

    _default_texture_path = default_material.texture_file_path;

    glfwSetFramebufferSizeCallback(_window, s_frame_buffer_resize_callback);

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
        _draw_frame();
    }
    Log::info("Render loop terminated.");
}

#pragma endregion PUBLIC_GFX_API

// PRIVATE.
#pragma region VULKAN_INSTANCE

bool
_check_validation_layer_support(
    std::span<char const*> const requested_validation_layers
) noexcept {
    auto available_layers = _vk_expect(
        vk::enumerateInstanceLayerProperties(),
        "Failed to enumerate instance layer properties count."
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
VulkanRenderer::_create_vk_instance() noexcept {
    u32 version = _vk_expect(
        vk::enumerateInstanceVersion(),
        "Failed to enumerate vulkan instance version."
    );

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

    vk::ApplicationInfo const app_info {
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

    vk::InstanceCreateInfo vk_instance_info {
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

        vk_instance_info.enabledLayerCount =
            static_cast<u32>(requested_layers.size());
        vk_instance_info.ppEnabledLayerNames = requested_layers.data();
    }

    _vk_instance = _vk_expect(
        vk::createInstanceUnique(vk_instance_info),
        "Failed to create Vulkan Instance"
    );

    Log::info(Log::LIGHT_GREEN, "Vulkan Instance successfully created.");
}

#pragma endregion

#pragma region SURFACE

void
VulkanRenderer::_create_surface() noexcept {
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

        core_assert(result == s_success, "Failed to check surface support");

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

    info.capabilities = _vk_expect(
        device.getSurfaceCapabilitiesKHR(*surface),
        "Failed to get surface capabilities"
    );

    Log::sub_info("Surface capabilities retrieved.");

    info.formats = _vk_expect(
        device.getSurfaceFormatsKHR(*surface),
        "Failed to get surface formats."
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

    info.present_modes = _vk_expect(
        device.getSurfacePresentModesKHR(*surface),
        "Failed to get present modes"
    );

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
VulkanRenderer::_create_swap_chain() noexcept {
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

    _swap_chain = _vk_expect(
        _device->createSwapchainKHRUnique(swap_chain_create_info),
        "Failed to create Swap Chain"
    );

    Log::info(Log::LIGHT_GREEN, "Swap Chain successfully created.");

    // Retrieve images.
    _swap_chain_images = _vk_expect(
        _device->getSwapchainImagesKHR(*_swap_chain),
        "Failed to retrieve swap chain images."
    );

    Log::info("Retrieved (", _swap_chain_images.size(), ") swap chain images.");

    // Store swap chain state.
    _swap_chain_image_format = surface_format.format;
    _swap_chain_extent = extent;
}

void
VulkanRenderer::_recreate_swap_chain() noexcept {
    int width {0};
    int height {0};

    glfwGetFramebufferSize(_window, &width, &height);

    // In case of window minimization, pause execution.
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(_window, &width, &height);
        glfwWaitEvents();
    }

    _vk_expect(_device->waitIdle(), "Failed to wait idle");

    _cleanup_swap_chain();

    _create_swap_chain();
    _create_image_views();
    _create_framebuffers();
}

void
VulkanRenderer::_cleanup_swap_chain() noexcept {
    for (auto& framebuffer : _swap_chain_framebuffers) {
        _device->destroyFramebuffer(*framebuffer, nullptr);
    }

    for (auto& image_view : _swap_chain_image_views) {
        _device->destroyImageView(*image_view, nullptr);
    }

    _device->destroySwapchainKHR(*_swap_chain);
}

#pragma endregion SWAP_CHAIN

#pragma region IMAGE_VIEWS

vk::UniqueImageView
VulkanRenderer::_create_image_view(
    vk::Image image,
    vk::Format format
) noexcept {
    vk::ImageViewCreateInfo const view_info {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
    };

    return _vk_expect(
        _device->createImageViewUnique(view_info),
        "Failed to create texture image view."
    );
}

void
VulkanRenderer::_create_image_views() noexcept {
    _swap_chain_image_views.resize(_swap_chain_images.size());

    for (usize i {}; i < _swap_chain_images.size(); ++i) {
        _swap_chain_image_views[i] =
            _create_image_view(_swap_chain_images[i], _swap_chain_image_format);
    }
}

#pragma endregion IMAGE_VIEWS

#pragma region PHYSICAL_DEVICE

bool
_check_device_extension_support(vk::PhysicalDevice const& device) {
    auto available_extensions = _vk_expect(
        device.enumerateDeviceExtensionProperties(),
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

    auto supported_features = device.getFeatures();

    bool const suitable = indices.is_complete() && extensions_supported &&
        swap_chain_adequate && supported_features.samplerAnisotropy;

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
VulkanRenderer::_create_physical_device() noexcept {
    core_assert(_surface.get(), "Surface is nullptr.");

    auto physical_devices = _vk_expect(
        _vk_instance->enumeratePhysicalDevices(),
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
VulkanRenderer::_create_logical_device() noexcept {
    QueueFamilyIndices const& queue_family_indices =
        _find_queue_families(_physical_device, _surface);

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos {};
    std::set<u32> const unique_queue_families = {
        queue_family_indices.graphics_family.value(),
        queue_family_indices.present_family.value(),
    };

    constexpr f32 queue_priority {1.0f};
    for (u32 const queue_family : unique_queue_families) {
        vk::DeviceQueueCreateInfo queue_create_info {
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };

        queue_create_infos.push_back(queue_create_info);
    }

    // Specify device features.
    constexpr vk::PhysicalDeviceFeatures device_features {
        .samplerAnisotropy = vk::True
    };

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

    _device = _vk_expect(
        _physical_device.createDeviceUnique(device_create_info),
        "Failed to create Logical Device"
    );
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

#pragma region DESCRIPTORS

void
VulkanRenderer::_create_descriptor_set_layout() noexcept {
    constexpr vk::DescriptorSetLayoutBinding ubo_layout_binding {
        // This binding is used in vertex shader.
        // layout (binding = 0) uniform UBO...
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        // Stages in which the descriptor is going to be referenced.
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .pImmutableSamplers = nullptr, // Optional. (for image sampling).
    };

    constexpr vk::DescriptorSetLayoutBinding sampler_layout_binding {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        // Sampler are usually used in fragment shaders, but they can also be
        // used in vertex shader, for example to dynamically deform a grid
        // of vertices by a heightmap.
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .pImmutableSamplers = nullptr,
    };

    std::array const bindings = {ubo_layout_binding, sampler_layout_binding};

    vk::DescriptorSetLayoutCreateInfo const layout_info {
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };

    _descriptor_set_layout = _vk_expect(
        _device->createDescriptorSetLayoutUnique(layout_info),
        "Failed to create Descriptor Set Layout."
    );
}

void
VulkanRenderer::_create_descriptor_pool() noexcept {
    std::array<vk::DescriptorPoolSize, 2> pool_sizes {};
    pool_sizes[0].descriptorCount = s_max_frames_in_flight;
    pool_sizes[1].descriptorCount = s_max_frames_in_flight;

    // Warning:
    // src: https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/02_Combined_image_sampler.html#:~:text=Inadequate%20descriptor%20pools%20are%20a,machines%2C%20but%20fails%20on%20others.
    vk::DescriptorPoolCreateInfo const pool_info {
        .flags = {},
        .maxSets = s_max_frames_in_flight,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };

    _descriptor_pool = _vk_expect(
        _device->createDescriptorPoolUnique(pool_info),
        "Failed to create Descriptor Pool."
    );
};

void
VulkanRenderer::_create_descriptor_sets() noexcept {
    std::vector<vk::DescriptorSetLayout> const layouts(
        s_max_frames_in_flight,
        *_descriptor_set_layout
    );

    vk::DescriptorSetAllocateInfo const alloc_info {
        .descriptorPool = *_descriptor_pool,
        .descriptorSetCount = s_max_frames_in_flight,
        .pSetLayouts = layouts.data(),
    };

    // Create one descriptor set for each frame in flight.
    auto [result, descriptor_sets] =
        _device->allocateDescriptorSetsUnique(alloc_info);
    core_assert(result == s_success, "Failed to allocate Descriptor Sets.");

    std::move(
        descriptor_sets.begin(),
        descriptor_sets.end(),
        _descriptor_sets.data()
    );

    // The descriptor sets have been allocated.
    // Now we need to configure the descriptor within them.
    for (usize i {}; i < s_max_frames_in_flight; ++i) {
        vk::DescriptorBufferInfo const descriptor_buffer_info {
            .buffer = *_uniform_buffers[i],
            .offset = 0,
            // If you're overwriting the whole buffer, like we are in this case,
            // then it is also possible to use the vk::WholeSize value for the range.
            .range = sizeof(UniformBufferObject)
        };

        vk::DescriptorImageInfo const image_info {
            .sampler = *_texture_sampler,
            .imageView = *_texture_image_view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };

        vk::WriteDescriptorSet const ubo_descriptor_write {
            // Descriptor set to update and it's binding.
            .dstSet = *_descriptor_sets[i],
            .dstBinding = 0,
            // Descriptor could be an array (not in this case).
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pImageInfo = nullptr, // Optional.
            .pBufferInfo = &descriptor_buffer_info,
            .pTexelBufferView = nullptr, // Optional.
        };

        vk::WriteDescriptorSet const sampler_descriptor_write {
            .dstSet = *_descriptor_sets[i],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_info,
            .pTexelBufferView = nullptr, // Optional.
        };

        std::array<vk::WriteDescriptorSet, 2> const descriptor_writes {
            ubo_descriptor_write,
            sampler_descriptor_write
        };

        // We can pass an array to make copies of the descriptor set.
        // But not for now so we set it as nullptr.
        _device->updateDescriptorSets(
            static_cast<u32>(descriptor_writes.size()),
            descriptor_writes.data(),
            // Copy stuff, disabled for now.
            0,
            nullptr
        );
    }
}

#pragma endregion DESCRIPTORS

#pragma region SHADERS

std::vector<u32>
VulkanRenderer::_compile_shader_to_spirv(
    std::string const& source,
    std::string const& file_path,
    shaderc_shader_kind kind
) noexcept {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(
        shaderc_target_env_vulkan,
        shaderc_env_version_vulkan_1_3
    );
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    Log::info("Compiling shader to SPIR-V.");
    Log::info("Shader source code:\n", source);

    shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(source, kind, file_path.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        Log::error("Shader compilation failed: ", result.GetErrorMessage());
        return {};
    }

    std::vector<u32> spirv(result.begin(), result.end());
    Log::info("Shader compiled successfully.");

    // Imprimir algunos bytes para depuración
    for (usize i = 0; i < std::min<usize>(spirv.size(), 10); ++i) {
        Log::info("SPIR-V byte: ", spirv[i]);
    }

    return spirv;
}

#pragma endregion

#pragma region GRAPHICS_PIPELINE

vk::UniqueShaderModule
_create_shader_module(
    vk::UniqueDevice const& logical_device,
    std::vector<u32> const& spirv_code
) {
    vk::ShaderModuleCreateInfo const shader_module_info {
        .codeSize = spirv_code.size() * sizeof(u32), // in bytes.
        .pCode = spirv_code.data(),
    };

    return _vk_expect(
        logical_device->createShaderModuleUnique(shader_module_info),
        "Failed to create shader module"
    );
}

void
VulkanRenderer::_create_render_pass() noexcept {
    vk::AttachmentDescription const color_attachment {
        .format = _swap_chain_image_format,
        .samples = vk::SampleCountFlagBits::e1, // Modify if using multi-sample.
        // Clear framebuffer to black beofre drawing a new frame.
        .loadOp = vk::AttachmentLoadOp::eClear,
        // We want to see the result so store the framebuffer information.
        .storeOp = vk::AttachmentStoreOp::eStore,
        // Stencil disabled for now.
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,

        // We don't care how images come, we'll clear them anyway.
        .initialLayout = vk::ImageLayout::eUndefined,
        // But we do care how they come out, in this case, as presentable images :)
        .finalLayout = vk::ImageLayout::ePresentSrcKHR,
    };

    // For now we'll declare only one sub-pass.
    constexpr vk::AttachmentReference color_attachment_ref {
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };

    vk::SubpassDescription const subpass {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref
    };

    constexpr vk::SubpassDependency subpass_dependency {
        // SubpassExternal: Implicit subpass before or after the render pass.
        .srcSubpass = vk::SubpassExternal,
        .dstSubpass = 0,

        // Wait for this operations to occur.
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
    };

    vk::RenderPassCreateInfo const render_pass_info {
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency,
    };

    _render_pass = _vk_expect(
        _device->createRenderPassUnique(render_pass_info),
        "Failed to create Render Pass"
    );
}

void
VulkanRenderer::_create_graphics_pipeline() noexcept {
    Log::header("Creating Graphics Pipeline.");

    // Set up shaders.
    Log::sub_info("Size: ", _default_vertex_shader_spirv.size());
    Log::sub_info("Size: ", _default_fragment_shader_spirv.size());

    vk::UniqueShaderModule vert_shader_module =
        _create_shader_module(_device, _default_vertex_shader_spirv);
    Log::info("Vertex shader module created.");

    vk::UniqueShaderModule frag_shader_module =
        _create_shader_module(_device, _default_fragment_shader_spirv);
    Log::info("Fragment shader module created.");

    // Create Render Pipeline.
    vk::PipelineShaderStageCreateInfo const vertex_shader_stage_info {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *vert_shader_module,
        .pName = "main", // Vertex shader code entry point.
        .pSpecializationInfo = nullptr, // We'll see later.
    };

    vk::PipelineShaderStageCreateInfo const fragment_shader_stage_info {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = *frag_shader_module,
        .pName = "main", // Fragment shader code entry point.
        .pSpecializationInfo = nullptr, // We'll see later.
    };

    std::array const shader_stages = {
        vertex_shader_stage_info,
        fragment_shader_stage_info,
    };

    constexpr auto binding_description = Vertex::binding_description();
    constexpr auto attribute_descriptions = Vertex::attribute_description();

    vk::PipelineVertexInputStateCreateInfo const vertex_input_info {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount =
            static_cast<u32>(attribute_descriptions.size()),
        .pVertexAttributeDescriptions = attribute_descriptions.data(),
    };

    constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
    };

    // Viewports define the transformations from vk::Image to framebuffer.
    vk::Viewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<f32>(_swap_chain_extent.width),
        .height = static_cast<f32>(_swap_chain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    // Difference between Viewport and Scissor:
    // src: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/02_Fixed_functions.html#:~:text=than%20the%20viewport.-,viewports%20scissors,-So%20if%20we
    vk::Rect2D scissor {
        .offset = {0, 0},
        .extent = _swap_chain_extent,
    };

    // Pipelines are "immutable", but some part might be modifiable at draw time,
    // Although this requires explicit implementation.
    constexpr std::array dynamic_pipeline_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state_info {
        .dynamicStateCount = static_cast<u32>(dynamic_pipeline_states.size()),
        .pDynamicStates = dynamic_pipeline_states.data(),
    };

    vk::PipelineViewportStateCreateInfo const viewport_state_info {
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    // Rasterizer.
    constexpr vk::PipelineRasterizationStateCreateInfo rasterizer_info {
        .depthClampEnable = vk::False, // Discard fragments beyond near/far.
        .rasterizerDiscardEnable = vk::False, // Geometry is rasterized lol.
        .polygonMode = vk::PolygonMode::eFill, // Fill polygons with fragments.
        // Culling.
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        // Depth.
        .depthBiasEnable = vk::False,
        .depthBiasConstantFactor = 0.0f, // Optional.
        .depthBiasClamp = 0.0f, // Optional.
        .depthBiasSlopeFactor = 0.0f, // Optional.
        .lineWidth = 1.0f,
    };

    // Multi-sample. ((e.g: for anti-alising)).
    constexpr vk::PipelineMultisampleStateCreateInfo multi_sampling_info {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
        .minSampleShading = 1.0f, // Optional.
        .pSampleMask = nullptr, // Optional.
        .alphaToCoverageEnable = vk::False, // Optional.
        .alphaToOneEnable = vk::False, // Optional.
    };

    // Depth and stencil.
    // WIP.

    // Color Blending.
    // (between current image and the one already present in the frame buffer).
    // Pseudo-code for how this works:
    /*
        if (blendEnable) {
            finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
            finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
        } else {
            finalColor = newColor;
        }

        finalColor = finalColor & colorWriteMask;

    // How Alpha blend works:
        finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
        finalColor.a = newAlpha.a;
    */
    constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
        .blendEnable = vk::False,

        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha, // Optional.
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha, // Optional.
        .colorBlendOp = vk::BlendOp::eAdd, // Optional.
        .srcAlphaBlendFactor = vk::BlendFactor::eOne, // Optional.
        .dstAlphaBlendFactor = vk::BlendFactor::eZero, // Optional.
        .alphaBlendOp = vk::BlendOp::eAdd, // Optional.

        // RGBA.
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA,
    };

    vk::PipelineColorBlendStateCreateInfo color_blending {
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy, // Optional.
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    color_blending.blendConstants[0] = 0.0f; // Optional.
    color_blending.blendConstants[1] = 0.0f; // Optional.
    color_blending.blendConstants[2] = 0.0f; // Optional.
    color_blending.blendConstants[3] = 0.0f; // Optional.

    // Pipeline Layout.
    vk::PipelineLayoutCreateInfo const pipeline_layout_info {
        // Uniform buffer object.
        .setLayoutCount = 1,
        .pSetLayouts = &(*_descriptor_set_layout),
        .pushConstantRangeCount = 0, // Optional.
        .pPushConstantRanges = nullptr, // Optional.
    };

    _pipeline_Layout = _vk_expect(
        _device->createPipelineLayoutUnique(pipeline_layout_info),
        "Failed to create pipeline layout."
    );

    Log::info("Pipeline layout created.");

    // FINALLY... IT'S ALIVE!!! THE RENDER PIPELINE!!!
    vk::GraphicsPipelineCreateInfo const graphics_pipeline_info {
        // Shader stages.
        .stageCount = 2, // Vertex and fragment.
        .pStages = shader_stages.data(),

        // Fixed-function stage.
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_state_info,
        .pRasterizationState = &rasterizer_info,
        .pMultisampleState = &multi_sampling_info,
        .pDepthStencilState = nullptr, // Optional.
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state_info,

        // Pipeline layout.
        .layout = *_pipeline_Layout,

        // Render pass.
        .renderPass = *_render_pass,
        .subpass = 0, // Index of the sub-pass, there is one so: zero.

        // Pipelines derivatives (fully optional).
        .basePipelineHandle = nullptr, // Optional.
        .basePipelineIndex = -1, // Optional.
    };

    _graphics_pipeline = _vk_expect(
        _device->createGraphicsPipelineUnique(nullptr, graphics_pipeline_info),
        "Failed to create Graphics Pipeline"
    );

    Log::info(Log::LIGHT_GREEN, "Graphics Pipeline successfully created.");
}

#pragma endregion

#pragma region FRAMEBUFFERS

void
VulkanRenderer::_create_framebuffers() noexcept {
    _swap_chain_framebuffers.resize(_swap_chain_image_views.size());

    for (usize i {}; i < _swap_chain_image_views.size(); ++i) {
        std::array const attachments = {*_swap_chain_image_views[i]};

        vk::FramebufferCreateInfo const framebuffer_info {
            .renderPass = *_render_pass,
            .attachmentCount = 1,
            .pAttachments = attachments.data(),
            .width = _swap_chain_extent.width,
            .height = _swap_chain_extent.height,
            .layers = 1,
        };

        _swap_chain_framebuffers[i] = _vk_expect(
            _device->createFramebufferUnique(framebuffer_info),
            "Failed to create Framebuffer"
        );
    }
}

#pragma endregion FRAMEBUFFERS

#pragma region COMMANDS

vk::CommandBuffer
VulkanRenderer::_begin_single_time_commands() noexcept {
    vk::CommandBufferAllocateInfo const alloc_info {
        .commandPool = *_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };

    auto const& command_buffers = _vk_expect(
        _device->allocateCommandBuffers(alloc_info),
        "Failed to allocate single time command buffer."
    );

    constexpr vk::CommandBufferBeginInfo begin_info {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };

    _vk_expect(
        command_buffers[0].begin(begin_info),
        "Failed to begin single time command buffer"
    );

    return command_buffers[0];
}

void
VulkanRenderer::_end_single_time_commands(vk::CommandBuffer& command_buffer
) noexcept {
    _vk_expect(command_buffer.end(), "Failed to end single time cmd buffer.");

    vk::SubmitInfo const submit_info {
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };

    _vk_expect(
        _graphics_queue.submit(submit_info),
        "Failed to submit cmd buffer to graphics queue."
    );

    _vk_expect(_graphics_queue.waitIdle(), "Failed to wait graphics queue.");

    _device->freeCommandBuffers(*_command_pool, command_buffer);
}

void
VulkanRenderer::_create_command_pool() noexcept {
    Log::header("Creating Command Pool.");

    QueueFamilyIndices const queue_family_indices =
        _find_queue_families(_physical_device, _surface);

    vk::CommandPoolCreateInfo const cmd_pool_info {
        // Allow command buffers to be re-recorded individually.
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        // We'll record graphics commands and submit them to the respective queue.
        .queueFamilyIndex = queue_family_indices.graphics_family.value(),
    };

    _command_pool = _vk_expect(
        _device->createCommandPoolUnique(cmd_pool_info),
        "Failed to create Cmd Pool."
    );

    Log::info(Log::LIGHT_GREEN, "Command Pool successfully created.");
}

void
VulkanRenderer::_create_command_buffers() noexcept {
    Log::header("Creating Command Buffer.");

    vk::CommandBufferAllocateInfo cmd_alloc_info {
        .commandPool = *_command_pool,
        // Primary: Can be submitted to queues, but not called from other cmd buffers.
        // Secondary: Cannot be submitted to queues, but can be call from a primery cmd buffer.
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = static_cast<u32>(_command_buffers.size()),
    };

    auto cmd_buffers = _vk_expect(
        _device->allocateCommandBuffersUnique(cmd_alloc_info),
        "Failed to allocate Command Buffer."
    );

    core_assert(
        _command_buffers.size() == cmd_buffers.size(),
        "Requested command buffers have different size than storage."
    );

    std::move(cmd_buffers.begin(), cmd_buffers.end(), _command_buffers.begin());
    Log::info(Log::LIGHT_GREEN, "Command Buffers successfully created.");
}

void
VulkanRenderer::_record_command_buffer(u32 image_index) noexcept {
    constexpr vk::CommandBufferBeginInfo begin_info {
        // Possible Flags:
        // vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        //      -> cmd buffer is re-recorded after execution.
        // vk::CommandBufferUsageFlagBits::ePassContinue
        //      -> used for secondary cmd buffers.
        // vk::CommandBufferUsageFlagBits::eSimultaneousUse
        //      -> cmd buffer can be re-submitted while execution is pending.
        .flags = {}, // Optional.

        // Used for secondary cmd buffers, they can inherit state from primary cmds.
        .pInheritanceInfo = nullptr, // Optional.
    };

    _vk_expect(
        _command_buffers[_current_frame]->begin(begin_info),
        "Failed to begin cmd record"
    );

    constexpr vk::ClearValue clear_value {
        vk::ClearColorValue {std::array {0.0f, 0.05f, 0.1f, 1.0f}}
    };

    // Start a render pass.
    vk::RenderPassBeginInfo const render_pass_begin_info {
        .renderPass = *_render_pass,
        .framebuffer = *_swap_chain_framebuffers[image_index],
        .renderArea =
            {
                .offset = {0, 0},
                .extent = {_swap_chain_extent},
            },

        // Clear color.
        .clearValueCount = 1,
        .pClearValues = &clear_value,
    };

    _command_buffers[_current_frame]->beginRenderPass(
        render_pass_begin_info,
        vk::SubpassContents::eInline // For primary commands.
    );

    // Let's start drawing!
    // Note: All vkCmds return void.

    // Bind graphics pipeline.
    _command_buffers[_current_frame]->bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *_graphics_pipeline
    );

    // Viewport and scissor are dynamic pipeline states :D
    vk::Viewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<f32>(_swap_chain_extent.width),
        .height = static_cast<f32>(_swap_chain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0,
    };

    constexpr u32 first_viewport {0};
    _command_buffers[_current_frame]->setViewport(first_viewport, viewport);

    vk::Rect2D scissor {
        .offset = {0, 0},
        .extent = _swap_chain_extent,
    };

    constexpr u32 first_scissor {0};
    _command_buffers[_current_frame]->setScissor(first_scissor, scissor);

    // Vertex Buffers.
    std::array const vertex_buffers {*_vertex_buffer};
    constexpr std::array<vk::DeviceSize, 1> offsets = {0};

    constexpr u32 first_binding {0};
    _command_buffers[_current_frame]
        ->bindVertexBuffers(first_binding, vertex_buffers, offsets);

    _command_buffers[_current_frame]
        ->bindIndexBuffer(*_index_buffer, 0, vk::IndexType::eUint16);

    _command_buffers[_current_frame]->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *_pipeline_Layout,
        0, // First set.
        1, // Descriptor set count.
        &(*_descriptor_sets[_current_frame]),
        0, // Dynamic offset count.
        nullptr // Dynamic offsets.
    );

    // D-D-D-Draaaaaaaawww call!!!!!!
    u32 const index_count = static_cast<u32>(_indices.size());
    _command_buffers[_current_frame]->drawIndexed(
        index_count,
        1, // Instance count.
        0, // First Index.
        0, // Offset.
        0 // First Instance.
    );

    // End.
    _command_buffers[_current_frame]->endRenderPass();
    _vk_expect(
        _command_buffers[_current_frame]->end(),
        "Failed to record cmd buffer."
    );
}

#pragma endregion COMMANDS

#pragma region SYNC_OBJECTS

void
VulkanRenderer::_create_sync_objects() noexcept {
    constexpr vk::SemaphoreCreateInfo semaphore_info {};
    constexpr vk::FenceCreateInfo fence_info {
        // Fence is "open" by default.
        // This is because in the first frame of the program we do not want
        // to wait for this fence to be signaled since no frame will be redendering.
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };

    for (usize i {}; i < s_max_frames_in_flight; ++i) {
        _image_available_semaphores[i] = _vk_expect(
            _device->createSemaphoreUnique(semaphore_info),
            "Failed to create Image Available Semaphore."
        );

        _render_finished_semapahores[i] = _vk_expect(
            _device->createSemaphoreUnique(semaphore_info),
            "Failed to create Render Finished Semaphore."
        );

        _frame_in_flight_fences[i] = _vk_expect(
            _device->createFenceUnique(fence_info),
            "Failed to create Frame In Flight Fence"
        );
    }
};

#pragma endregion SYNC_OBJECTS

#pragma region DRAW

void
VulkanRenderer::_draw_frame() noexcept {
    // Rendering a frame consists in:
    // - Wait for the previous frame to finish.
    // - Acquire an image from the swap chain.
    // - Record a command buffer which draws the scene onto that image.
    // - Submit the recorded command buffer.
    // - Present the swap chain image.

    // Synchronization.
    // All function calls to the GPU are processed asynchronously,
    // so we must explicity tell Vulkan the order of execution of:
    // - Acquire an image from the swap chain
    // - Execute commands that draw onto the acquired image
    // - Present that image to the screen for presentation, returning it to the swapchain.

    // One option for *GPU (Device)* synchronization: Semaphores (Binary in this case).
    // src: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/02_Rendering_and_presentation.html#:~:text=the%20desired%20ordering.-,Semaphores,-A%20semaphore%20is

    // One option for *CPU (Host)* synchronization: Fences.
    // src: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/02_Rendering_and_presentation.html#:~:text=will%20now%20describe.-,Fences,-A%20fence%20has

    // Wait for current frame to finish rendering.
    // Obviously first frame won't be waited because fence is signaled (open)
    // in first frame.
    constexpr vk::Bool32 wait_for_all {vk::True};
    constexpr u64 time_out_ns {std::numeric_limits<u64>::max()};
    _vk_expect(
        _device->waitForFences(
            *_frame_in_flight_fences[_current_frame],
            wait_for_all,
            time_out_ns
        ),
        "Failed to wait for frame in flight fence."
    );

    auto image_index = _vk_expect(
        _device->acquireNextImageKHR(
            *_swap_chain,
            time_out_ns,
            *_image_available_semaphores[_current_frame],
            nullptr
        ),
        "Failed to acquire image from swapchain."
    );

    // Reset fence for next frame.
    _vk_expect(
        _device->resetFences(*_frame_in_flight_fences[_current_frame]),
        "Failed to reset frame in flight fence."
    );

    // Make sure cmd buffer is in default state.
    _vk_expect(
        _command_buffers[_current_frame]->reset(),
        "Failed to reset cmd buffer"
    );

    // Draw to the image :)
    _record_command_buffer(image_index);

    _update_uniform_buffer(_current_frame);

    std::array const wait_semaphores = {
        *_image_available_semaphores[_current_frame]
    };
    std::array const signal_semaphores = {
        *_render_finished_semapahores[_current_frame]
    };
    constexpr vk::PipelineStageFlags wait_pipelines_stages =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::SubmitInfo const submit_info {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_semaphores.data(),
        // Wait for color rendering to finish.
        .pWaitDstStageMask = &wait_pipelines_stages,
        .commandBufferCount = 1,
        .pCommandBuffers =
            &_command_buffers[_current_frame].get(), // Pointer is const.
        // Which semaphores to signal (green light) once cmd buffer finishes.
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_semaphores.data(),
    };

    _vk_expect(
        _graphics_queue
            .submit(submit_info, *_frame_in_flight_fences[_current_frame]),
        "Failed to submit to graphics queue"
    );

    // Presentation.
    std::array const swap_chains = {*_swap_chain};

    vk::PresentInfoKHR present_info {
        // Semaphores to wait before presentation.
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signal_semaphores.data(),
        .swapchainCount = 1,
        .pSwapchains = swap_chains.data(),
        .pImageIndices = &image_index,
        .pResults = nullptr, // Optional.
    };

    auto result_present = _present_queue.presentKHR(present_info);

    // Suboptimal: The swapchain can still be used to successfully present
    // the surface, but the surface properties are no longer matched exactly.
    if (result_present == vk::Result::eErrorOutOfDateKHR ||
        result_present == vk::Result::eSuboptimalKHR || _framebuffer_resized) {
        _framebuffer_resized = false;
        _recreate_swap_chain();

        return;
    }

    // Advance to the next frame.
    _current_frame = (_current_frame + 1) % s_max_frames_in_flight;
}

#pragma endregion DRAW

#pragma region BUFFERS

u32
_find_memory_type(
    vk::PhysicalDevice const& physical_device,
    u32 mem_type_filter,
    vk::MemoryPropertyFlags const& properties
) noexcept {
    Log::info("Querying available memory types.");

    // Query available types of memory.
    auto const& mem_properties = physical_device.getMemoryProperties();

    for (u32 i {}; i < mem_properties.memoryTypeCount; ++i) {
        // Imagine mem_type_filter = 0000 0100
        // we will shift a 1 to left to check if it matches with the mask.
        // iteration i = 0:
        //      test   = ...0000 0001 (1 << 0)
        //    & filter = ...0000 0100 = 0 (FALSE)
        // iteration i = 1:
        //      test   = ...0000 0010 (1 << 1)
        //    & filter = ...0000 0100 = 0 (FALSE)
        // iteration i = 0:
        //      test   = ...0000 0100 (1 << 2)
        //    & filter = ...0000 0100 = 4 (TRUE, because any number != 0 = true).
        u32 const test_mask = (1 << i);
        // Hey you. yeah you. research about !! in c++, it's a banger.

        auto const& mem_property_flags =
            mem_properties.memoryTypes[i].propertyFlags;

        Log::sub_info(
            "Checking memory type ",
            i,
            ": ",
            vk::to_string(mem_property_flags)
        );

        if ((mem_type_filter & test_mask) &&
            (mem_property_flags & properties) == properties) {
            Log::info("Suitable memory type found: ", i, ".");
            return i;
        }
    }

    core_assert(false, "Failed to find suitable memory type.");
    return -1;
}

void
VulkanRenderer::_copy_buffer(
    vk::UniqueBuffer& src_buffer,
    vk::UniqueBuffer& dst_buffer,
    vk::DeviceSize const size
) noexcept {
    auto cmd_buffer = _begin_single_time_commands();

    vk::BufferCopy const copy_region {
        .srcOffset = 0, // Optional.
        .dstOffset = 0, // Optional.
        .size = size,
    };

    cmd_buffer.copyBuffer(*src_buffer, *dst_buffer, copy_region);

    _end_single_time_commands(cmd_buffer);
}

void
VulkanRenderer::_create_buffer_unique(
    vk::DeviceSize const size,
    vk::BufferUsageFlags const usage,
    vk::MemoryPropertyFlags const properties,
    vk::UniqueBuffer& buffer,
    vk::UniqueDeviceMemory& buffer_memory
) noexcept {
    vk::BufferCreateInfo buffer_info {
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };

    buffer = _vk_expect(
        _device->createBufferUnique(buffer_info),
        "Failed to create buffer"
    );

    auto const& buffer_mem_requirements =
        _device->getBufferMemoryRequirements(*buffer);

    vk::MemoryAllocateInfo const alloc_info {
        .allocationSize = buffer_mem_requirements.size,
        .memoryTypeIndex = _find_memory_type(
            _physical_device,
            buffer_mem_requirements.memoryTypeBits,
            properties
        ),
    };

    buffer_memory = _vk_expect(
        _device->allocateMemoryUnique(alloc_info),
        "Failed to allocate buffer memory."
    );

    // Bind buffer.
    // Since the memory is allocated specifically for this buffer,
    // the offset is 0. If the offset is non-zero, then it is required to be
    // divisible by mem_requirements.alignment.
    // That can be check using: if (mem_requirements.aligment % mem_offset == 0).
    // Note: vk::DeviceSize is 64-bits.
    constexpr vk::DeviceSize mem_offset {0UL};
    _vk_expect(
        _device->bindBufferMemory(*buffer, *buffer_memory, mem_offset),
        "Failed to bind buffer."
    );
}

void
VulkanRenderer::_create_vertex_buffer() noexcept {
    Log::header("Creating Vertex Buffer.");
    vk::DeviceSize const buffer_size = sizeof(_vertices[0]) * _vertices.size();
    Log::info("Vertex Buffer size: ", buffer_size);

    constexpr auto staging_usage = vk::BufferUsageFlagBits::eTransferSrc;
    constexpr auto staging_properties =
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;

    vk::UniqueBuffer staging_buffer {};
    vk::UniqueDeviceMemory staging_buffer_memory {};

    // Create Staging Buffer.
    _create_buffer_unique(
        buffer_size,
        staging_usage,
        staging_properties,
        staging_buffer,
        staging_buffer_memory
    );
    Log::info("Vertex Staging Buffer created.");

    constexpr vk::DeviceSize offset {0};
    auto [result_map, data] =
        _device->mapMemory(*staging_buffer_memory, offset, buffer_size);
    core_assert(
        result_map == s_success,
        "Failed to map Staging Buffer memory."
    );
    Log::info("Vertex Staging Buffer memory mapped.");

    std::memcpy(data, _vertices.data(), static_cast<usize>(buffer_size));
    Log::info("Vertex Staging Buffer data copied to GPU.");

    _device->unmapMemory(*staging_buffer_memory);
    Log::info("Vertex Staging Buffer memory unmapped.");

    constexpr auto vertex_usage = vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eVertexBuffer;
    constexpr auto vertex_properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    // Create Vertex Buffer.
    _create_buffer_unique(
        buffer_size,
        vertex_usage,
        vertex_properties,
        _vertex_buffer,
        _vertex_buffer_memory
    );
    Log::info("Vertex Buffer created.");

    _copy_buffer(staging_buffer, _vertex_buffer, buffer_size);
    Log::info("Vertex Buffer data copied from Staging Buffer.");
}

void
VulkanRenderer::_create_index_buffer() noexcept {
    Log::header("Creating Index Buffer.");
    vk::DeviceSize const buffer_size = sizeof(_indices[0]) * _indices.size();
    Log::info("Index Buffer size: ", buffer_size);

    constexpr auto staging_usage = vk::BufferUsageFlagBits::eTransferSrc;
    constexpr auto staging_properties =
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;

    vk::UniqueBuffer staging_buffer {};
    vk::UniqueDeviceMemory staging_buffer_memory {};

    // Create Staging Buffer.
    _create_buffer_unique(
        buffer_size,
        staging_usage,
        staging_properties,
        staging_buffer,
        staging_buffer_memory
    );
    Log::info("Index Staging Buffer created.");

    constexpr vk::DeviceSize offset {0};
    void* data = _vk_expect(
        _device->mapMemory(*staging_buffer_memory, offset, buffer_size),
        "Failed to map Staging Buffer Memory."
    );
    Log::info("Index Staging Buffer memory mapped.");

    std::memcpy(data, _indices.data(), static_cast<usize>(buffer_size));
    Log::info("Index Staging Buffer data copied to GPU.");

    _device->unmapMemory(*staging_buffer_memory);
    Log::info("Index Staging Buffer memory unmapped.");

    constexpr auto index_usage = vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eIndexBuffer;
    constexpr auto index_properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    // Create Index Buffer.
    _create_buffer_unique(
        buffer_size,
        index_usage,
        index_properties,
        _index_buffer,
        _index_buffer_memory
    );
    Log::info("Index Buffer created.");

    _copy_buffer(staging_buffer, _index_buffer, buffer_size);
}

void
VulkanRenderer::_create_uniform_buffers() noexcept {
    constexpr vk::DeviceSize buffer_size = sizeof(UniformBufferObject);

    for (usize i {}; i < s_max_frames_in_flight; ++i) {
        _create_buffer_unique(
            buffer_size,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
            _uniform_buffers[i],
            _uniform_buffers_memory[i]
        );

        constexpr u32 offset {0};
        _uniform_buffers_mapped[i] = _vk_expect(
            // Persistent mapping.
            _device
                ->mapMemory(*_uniform_buffers_memory[i], offset, buffer_size),
            "Failed to map memory for uniform buffer object."
        );
    }
}

void
VulkanRenderer::_update_uniform_buffer(u32 current_image) noexcept {
    using clock = std::chrono::high_resolution_clock;
    using period = std::chrono::seconds::period;
    static auto start_time = clock::now();

    auto current_time = clock::now();
    f32 time =
        std::chrono::duration<f32, period>(current_time - start_time).count();

    UniformBufferObject ubo {};
    // Model matrix it's a simple rotation around de Z-axis.
    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        time * glm::radians(90.0f),
        glm::vec3(0.0f, 0.0f, 0.2f)
    );

    // View matrix it's simply a view from above at a 45 degree angle.
    ubo.view = glm::lookAt(
        glm::vec3(2.0f, 2.0f, 2.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    f32 const aspect_ratio =
        _swap_chain_extent.width / static_cast<f32>(_swap_chain_extent.height);
    constexpr f32 near {0.1f};
    constexpr f32 far {100.0f};
    constexpr f32 vertical_fov = glm::radians(45.0f);
    ubo.projection = glm::perspective(vertical_fov, aspect_ratio, near, far);
    /* GLM was originally designed for OpenGL, where the Y coordinate of the clip 
       coordinates is inverted. The easiest way to compensate for that is to flip 
       the sign on the scaling factor of the Y axis in the projection matrix.
        If you don’t do this, then the image will be rendered upside down.
    */
    ubo.projection[1][1] *= -1;

    // Remember ubo memory uses persistent mapping.
    std::memcpy(_uniform_buffers_mapped[current_image], &ubo, sizeof(ubo));
}

#pragma endregion BUFFERS

#pragma region TEXTURES

void
VulkanRenderer::_create_image(
    u32 width,
    u32 height,
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::UniqueImage& image,
    vk::UniqueDeviceMemory& image_memory
) noexcept {
    vk::ImageCreateInfo const image_info {
        .flags = {}, // Optional.
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1, // No multi-sampling.
        .tiling = tiling,
        .usage = usage,
        // Image will only be used by one queue family; graphics.
        .sharingMode = vk::SharingMode::eExclusive,
        // eUndefined: Not usable by the GPU and the very first transition will discrad the texels.
        // ePreinitialized: Not usable by the GPU, but the first transition will preserve the texels.
        // Preinitialized should be use along eLinear ImageTiling.
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    _texture_image = _vk_expect(
        _device->createImageUnique(image_info),
        "Failed to create default texture."
    );

    vk::MemoryRequirements const texture_mem_requirements =
        _device->getImageMemoryRequirements(*_texture_image);

    vk::MemoryAllocateInfo const texture_alloc_info {
        .allocationSize = texture_mem_requirements.size,
        .memoryTypeIndex = _find_memory_type(
            _physical_device,
            texture_mem_requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        ),
    };

    _texture_image_memory = _vk_expect(
        _device->allocateMemoryUnique(texture_alloc_info),
        "Failed to allocate image memory"
    );

    _vk_expect(
        _device->bindImageMemory(*_texture_image, *_texture_image_memory, 0),
        "Failed to bind image memory."
    );
}

void
VulkanRenderer::_copy_buffer_to_image(
    vk::UniqueBuffer& buffer,
    vk::UniqueImage& image,
    u32 width,
    u32 height
) noexcept {
    Log::info("Copying buffer to image.");

    vk::BufferImageCopy const region {
        // Buffer byte offset where pixel values start.
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };

    auto command_buffer = _begin_single_time_commands();

    command_buffer.copyBufferToImage(
        *buffer,
        *image,
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &region
    );

    _end_single_time_commands(command_buffer);

    Log::info("Buffer successfully copied to image.");
}

void
VulkanRenderer::_transition_image_layout(
    vk::Image image,
    vk::Format format,
    vk::ImageLayout old_layout,
    vk::ImageLayout new_layout
) noexcept {
    auto command_buffer = _begin_single_time_commands();

    // Use a barrier to control access to resources (Images, Buffers).
    vk::ImageMemoryBarrier image_barrier {
        .oldLayout = old_layout,
        .newLayout = new_layout,
        // If we are using the barrier to transfer queue family ownership,
        // then these two fields should be the indices of the queue families.
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange =
            {.aspectMask = vk::ImageAspectFlagBits::eColor,
             .baseMipLevel = 0,
             .levelCount = 1,
             .baseArrayLayer = 0,
             .layerCount = 1},
    };

    vk::PipelineStageFlags source_stage {};
    vk::PipelineStageFlags destination_stage {};

    if (old_layout == vk::ImageLayout::eUndefined &&
        new_layout == vk::ImageLayout::eTransferDstOptimal) {
        image_barrier.srcAccessMask = {},
        image_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        source_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        destination_stage = vk::PipelineStageFlagBits::eTransfer;
    } else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
               new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        image_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        image_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        source_stage = vk::PipelineStageFlagBits::eTransfer;
        destination_stage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        core_assert(false, "Unsupported layout transition.");
    }

    // Transfer stage is a pseude-stage. See More:
    // src: https://docs.vulkan.org/spec/latest/chapters/synchronization.html#VkPipelineStageFlagBits
    command_buffer.pipelineBarrier(
        // Allowed values:
        // src: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-access-types-supported
        source_stage, // In which pipeline stage the operations happen?
        destination_stage, // In which pipeline stage will the operations wait?
        vk::DependencyFlags {},
        nullptr,
        nullptr,
        image_barrier
    );

    _end_single_time_commands(command_buffer);
}

void
VulkanRenderer::_create_texture_image() noexcept {
    int tex_width {0};
    int tex_height {0};
    int tex_channels {0};

    stbi_uc* pixels = stbi_load(
        _default_texture_path.c_str(),
        &tex_width,
        &tex_height,
        &tex_channels,
        STBI_rgb_alpha
    );

    // The pixels are laid out row by row with 4 bytes per pixel in the case
    // of STBI_rgb_alpha for a total of tex_width * tex_height * 4 values.
    vk::DeviceSize const image_size = tex_width * tex_height * 4;

    core_assert(pixels, "Failed to load texture image.");

    // Allocate buffer for image.
    vk::UniqueBuffer staging_buffer {};
    vk::UniqueDeviceMemory staging_buffer_memory {};

    constexpr auto staging_usage = vk::BufferUsageFlagBits::eTransferSrc;
    constexpr auto staging_properties =
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;
    _create_buffer_unique(
        image_size,
        staging_usage,
        staging_properties,
        staging_buffer,
        staging_buffer_memory
    );

    void* data = _vk_expect(
        _device->mapMemory(*staging_buffer_memory, 0, image_size),
        "Failed to map Texture Staging Buffer Memory"
    );

    std::memcpy(data, pixels, static_cast<usize>(image_size));
    _device->unmapMemory(*staging_buffer_memory);

    stbi_image_free(pixels);

    // We should use the same format as the pixels (STBI_rgb_alpha).
    // It is possible that the eR8G8B8A8Srgb format is not supported by the
    // graphics hardware, we should have a list of acceptable alternatives.
    constexpr vk::Format image_format {vk::Format::eR8G8B8A8Srgb};

    _create_image(
        tex_width,
        tex_height,
        image_format,
        // eLinear: Texels are laid out in row-major order like the pixels array.
        // eOptimal: Texels are laid out in an implementation defined order.
        // Note: If we want to directly acces texels in the image's memory,
        // then you must use eLinear.
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        _texture_image,
        _texture_image_memory
    );

    _transition_image_layout(
        *_texture_image,
        image_format,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal
    );

    _copy_buffer_to_image(
        staging_buffer,
        _texture_image,
        static_cast<u32>(tex_width),
        static_cast<u32>(tex_height)
    );

    _transition_image_layout(
        *_texture_image,
        image_format,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
}

void
VulkanRenderer::_create_texture_image_view() noexcept {
    _texture_image_view =
        _create_image_view(*_texture_image, vk::Format::eR8G8B8A8Srgb);
}

#pragma endregion TEXTURES

#pragma region TEXTURE_SAMPLER

void
VulkanRenderer::_create_texture_sampler() noexcept {
    auto physical_device_properties = _physical_device.getProperties();

    vk::SamplerCreateInfo const sampler_info {
        // How to interpolate texels that are magnified or minified.
        // Maginification occurs when there is oversampling.
        // Minifification occurs when there is undersampling.
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        // See address modes visulization.
        // src: https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/01_Image_view_and_sampler.html#:~:text=The%20image%20below%20displays%20some%20of%20the%20possibilities:
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        // Mipmapping stuff, all disabled for now.
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::True,
        // A lower value results in better performance, but lower quality results.
        // We're going to use the max available for the selected physical device.
        .maxAnisotropy = physical_device_properties.limits.maxSamplerAnisotropy,
        // If a comparison function is enabled, then texels will first be compared
        // to a value, and the result of that comparison is used in filtering operations.
        // This is mainly used for percentage-closer filtering on shadow maps.
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        // Which color to return when sampling beyond the image with clamp
        // to border addressing mode. (You cannot use an arbitrary color).
        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
        // Specifies the coordinate system for addressing texels.
        // vk::True: Use [0, texWidth) and [0, texHeight) range.
        // vk::False: Use [0, 1) range on all axes.
        // Normalized coordinates (VK_FALSE) are preferred for varying resolutions.
        .unnormalizedCoordinates = vk::False,
    };

    _texture_sampler = _vk_expect(
        _device->createSamplerUnique(sampler_info),
        "Failed to create texture sampler."
    );
}

#pragma endregion TEXTURE_SAMPLER

} // namespace core