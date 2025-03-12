#include <core/renderers/vulkan_renderer.hpp>

#include <algorithm> // clamp.
#include <array>
#include <cstring> // strcmp.
#include <filesystem>
#include <limits>
#include <map> // multipmap.
#include <set>
#include <span>

namespace core {

static constexpr vk::Result s_success = vk::Result::eSuccess;
static constexpr u32 s_max_frames_in_flight = 2;
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
        _draw_frame();
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

    v_assert(
        enumeration_result == s_success,
        "Failed to enumerate instance layer properties count."
    );

    std::vector<vk::LayerProperties> available_layers(layer_count);

    vk::Result initialization_result = vk::enumerateInstanceLayerProperties(
        &layer_count,
        available_layers.data()
    );

    v_assert(
        initialization_result == s_success,
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
VulkanRenderer::_create_vk_instance() noexcept {
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

        v_assert(
            _check_validation_layer_support(requested_layers),
            "Validation layers requested but not available, please install them."
        );

        instance_create_info.enabledLayerCount =
            static_cast<u32>(requested_layers.size());
        instance_create_info.ppEnabledLayerNames = requested_layers.data();
    }

    auto [result, instance] = vk::createInstanceUnique(instance_create_info);

    v_assert(result == s_success, "Failed to create VK Instance.");

    _vk_instance = std::move(instance);
    Log::info(Log::LIGHT_GREEN, "Vulkan Instance successfully created.");
}

#pragma endregion

#pragma region SURFACE

void
VulkanRenderer::_create_surface() noexcept {
    VkSurfaceKHR c_surface;
    v_assert(
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

        v_assert(result == s_success, "Failed to check surface support");

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
    v_assert(result1 == s_success, "Failed to get surface capabilities");

    info.capabilities = capabilities;

    Log::sub_info("Surface capabilities retrieved.");

    auto [result2, formats] = device.getSurfaceFormatsKHR(*surface);
    v_assert(result2 == s_success, "Failed to get surface formats");

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
    v_assert(result3 == s_success, "Failed to get present modes");

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

    v_assert(_logical_device.get(), "Device is nullptr, please initialize it.");
    auto [result, swap_chain] =
        _logical_device.get().createSwapchainKHRUnique(swap_chain_create_info);

    v_assert(result == s_success, "Failed to create Swap Chain");
    _swap_chain = std::move(swap_chain);

    Log::info(Log::LIGHT_GREEN, "Swap Chain successfully created.");

    // Retrieve images.
    auto [result2, images] =
        _logical_device->getSwapchainImagesKHR(*_swap_chain);

    v_assert(result2 == s_success, "Failed to retrieve swap chain images.");

    Log::info("Retrieved (", images.size(), ") swap chain images.");

    _swap_chain_images = std::move(images);

    // Store swap chain state.
    _swap_chain_image_format = surface_format.format;
    _swap_chain_extent = extent;
}

#pragma endregion SWAP_CHAIN

#pragma region IMAGE_VIEWS

void
VulkanRenderer::_create_image_views() noexcept {
    _swap_chain_image_views.resize(_swap_chain_images.size());

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

        v_assert(result == s_success, "Failed to create unique image view.");

        _swap_chain_image_views[i] = std::move(image_view);
    }
}

#pragma endregion IMAGE_VIEWS

#pragma region PHYSICAL_DEVICE

bool
_check_device_extension_support(vk::PhysicalDevice const& device) {
    auto [result, available_extensions] =
        device.enumerateDeviceExtensionProperties();

    v_assert(
        result == s_success,
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
VulkanRenderer::_create_physical_device() noexcept {
    v_assert(_surface.get(), "Surface is nullptr.");

    auto [result, physical_devices] = _vk_instance->enumeratePhysicalDevices();

    v_assert(result == s_success, "Failed to find GPUs with Vulkan support.");

    Log::info("Found (", physical_devices.size(), ") Physical devices:");
    for (auto const& device : physical_devices) {
        Log::sub_info(device.getProperties().deviceName);

        if (_is_device_suitable(device, _surface)) {
            _physical_device = device;
            break;
        }
    }

    v_assert(_physical_device, "Failed to find a suitable GPU.");

    std::multimap<u32, vk::PhysicalDevice> candidates {};

    for (auto const& device : physical_devices) {
        u32 const score = _rate_device_suitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    v_assert(candidates.rbegin()->first > 0, "Failed to rate GPUs.");

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

    v_assert(result == s_success, "Failed to create Logical Device");

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

#pragma region GRAPHICS_PIPELINE

vk::UniqueShaderModule
_create_shader_module(
    vk::UniqueDevice const& logical_device,
    std::vector<c8> const& code
) {
    vk::ShaderModuleCreateInfo shader_module_create_info {
        .codeSize = code.size(),
        .pCode = reinterpret_cast<u32 const*>(code.data()),
    };

    auto [result, shader_module] =
        logical_device->createShaderModuleUnique(shader_module_create_info);

    v_assert(result == s_success, "Failed to create shader module");

    return std::move(shader_module);
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

    auto [result, render_pass] =
        _logical_device->createRenderPassUnique(render_pass_info);

    v_assert(result == s_success, "Failed to create Render Pass.");

    _render_pass = std::move(render_pass);
}

void
VulkanRenderer::_create_graphics_pipeline() noexcept {
    Log::header("Creating Graphics Pipeline.");

    // Set up shaders.
    // FIXME - Use non-harcode absolute filepath.
    auto vert_shader_code = read_file(
        "/mnt/sara01/dev/v-engine/engine/shaders/compiled/basic_vert.spv"
    );
    Log::info("Loaded vert shader code.");
    Log::sub_info("Size: ", vert_shader_code.size());

    auto frag_shader_code = read_file(
        "/mnt/sara01/dev/v-engine/engine/shaders/compiled/basic_frag.spv"
    );
    Log::info("Loaded frag shader code.");
    Log::sub_info("Size: ", frag_shader_code.size());

    vk::UniqueShaderModule vert_shader_module =
        _create_shader_module(_logical_device, vert_shader_code);
    Log::info("Vertex shader module created.");

    vk::UniqueShaderModule frag_shader_module =
        _create_shader_module(_logical_device, frag_shader_code);
    Log::info("Fragment shader module created.");

    // Shader code is no longer necessary after creating shader modules.
    vert_shader_code.clear();
    vert_shader_code.shrink_to_fit();
    frag_shader_code.clear();
    frag_shader_code.shrink_to_fit();

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

    constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_info {
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
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
        .frontFace = vk::FrontFace::eClockwise,
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
    // Empty for now.
    constexpr vk::PipelineLayoutCreateInfo pipeline_layout_info {
        .setLayoutCount = 0, // Optional.
        .pSetLayouts = nullptr, // Optional.
        .pushConstantRangeCount = 0, // Optional.
        .pPushConstantRanges = nullptr, // Optional.
    };

    auto [result, pipeline_layout] =
        _logical_device->createPipelineLayoutUnique(pipeline_layout_info);

    v_assert(result == s_success, "Failed to create pipeline layout.");

    _pipeline_Layout = std::move(pipeline_layout);
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

    auto [result2, graphics_pipeline] =
        _logical_device->createGraphicsPipelineUnique(
            nullptr, // Pipeline Cache.
            graphics_pipeline_info
        );

    v_assert(result2 == s_success, "Failed to create Graphics Pipeline.");

    _graphics_pipeline = std::move(graphics_pipeline);
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

        auto [result, framebuffer] =
            _logical_device->createFramebufferUnique(framebuffer_info);

        v_assert(result == s_success, "Failed to create Framebuffer");

        _swap_chain_framebuffers[i] = std::move(framebuffer);
    }
}

#pragma endregion FRAMEBUFFERS

#pragma region COMMANDS

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

    auto [result, cmd_pool] =
        _logical_device->createCommandPoolUnique(cmd_pool_info);

    v_assert(result == s_success, "Failed to create Cmd Pool.");
    _command_pool = std::move(cmd_pool);

    Log::info(Log::LIGHT_GREEN, "Command Pool successfully created.");
}

void
VulkanRenderer::_create_command_buffer() noexcept {
    Log::header("Creating Command Buffer.");

    vk::CommandBufferAllocateInfo cmd_alloc_info {
        .commandPool = *_command_pool,
        // Primary: Can be submitted to queues, but not called from other cmd buffers.
        // Secondary: Cannot be submitted to queues, but can be call from a primery cmd buffer.
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };

    auto [result, cmd_buffers] =
        _logical_device->allocateCommandBuffersUnique(cmd_alloc_info);

    v_assert(result == s_success, "Failed to allocate Command Buffer.");

    _command_buffer = std::move(cmd_buffers[0]);

    Log::info(Log::LIGHT_GREEN, "Command Buffer successfully created.");
}

void
VulkanRenderer::_record_command_buffer(u32 const image_index) noexcept {
    Log::header("Recording Command Buffer.");

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

    auto result_begin = _command_buffer->begin(begin_info);
    v_assert(result_begin == s_success, "Failed to begin cmd record");

    Log::info("Command Buffer recording started.");

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

    _command_buffer->beginRenderPass(
        render_pass_begin_info,
        vk::SubpassContents::eInline // For primary commands.
    );

    Log::info("Render pass started.");

    // Let's start drawing!
    // Note: All vkCmds return void.

    // Bind graphics pipeline.
    _command_buffer->bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *_graphics_pipeline
    );

    Log::info("Graphics pipeline bound.");

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
    _command_buffer->setViewport(first_viewport, viewport);

    Log::info("Viewport set.");

    vk::Rect2D scissor {
        .offset = {0, 0},
        .extent = _swap_chain_extent,
    };

    constexpr u32 first_scissor {0};
    _command_buffer->setScissor(first_scissor, scissor);

    Log::info("Scissor set.");

    // D-D-D-Draaaaaaaawww!!!!!!
    constexpr u32 vertex_count {3};
    constexpr u32 instance_count {3};
    constexpr u32 first_vertex {0}; // Defines min value of gl_VertexIndex.
    constexpr u32 first_instance {0};

    _command_buffer
        ->draw(vertex_count, instance_count, first_vertex, first_instance);

    Log::info("Draw command issued.");

    // End.
    _command_buffer->endRenderPass();
    auto result_end = _command_buffer->end();
    v_assert(result_end == s_success, "Failed to record cmd buffer.");

    Log::info(Log::LIGHT_GREEN, "Command Buffer recording completed.");
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

    auto [result1, image_available_semaphore] =
        _logical_device->createSemaphoreUnique(semaphore_info);
    v_assert(
        result1 == s_success,
        "Failed to create Image Available Semaphore."
    );

    auto [result2, render_finished_semaphore] =
        _logical_device->createSemaphoreUnique(semaphore_info);
    v_assert(
        result2 == s_success,
        "Failed to create Render Finished Semaphore."
    );

    _image_available_semaphore = std::move(image_available_semaphore);
    _render_finished_semapahore = std::move(render_finished_semaphore);

    auto [result3, frame_in_flight_fence] =
        _logical_device->createFenceUnique(fence_info);
    v_assert(result3 == s_success, "Failed to create Frame In Flight Fence");

    _frame_in_flight_fence = std::move(frame_in_flight_fence);
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
    auto result_wait = _logical_device->waitForFences(
        *_frame_in_flight_fence,
        wait_for_all,
        time_out_ns
    );
    v_assert(
        result_wait == s_success,
        "Failed to wait for frame in flight fence."
    );

    // Reset fence for next frame.
    _logical_device->resetFences(*_frame_in_flight_fence);

    auto [result, image_index] = _logical_device->acquireNextImageKHR(
        *_swap_chain,
        time_out_ns,
        *_image_available_semaphore,
        nullptr
    );
    v_assert(result == s_success, "Failed to acquire image from swapchain.");

    // Make sure cmd buffer is in default state.
    auto result_reset = _command_buffer->reset();
    v_assert(result_reset == s_success, "Failed to reset cmd buffer");

    // Draw to the image :)
    _record_command_buffer(image_index);

    std::array const wait_semaphores = {*_image_available_semaphore};
    std::array const signal_semaphores = {*_render_finished_semapahore};
    constexpr vk::PipelineStageFlags wait_pipelines_stages =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::SubmitInfo const submit_info {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_semaphores.data(),
        // Wait for color rendering to finish.
        .pWaitDstStageMask = &wait_pipelines_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &_command_buffer.get(), // Pointer is const.
        // Which semaphores to signal (green light) once cmd buffer finishes.
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_semaphores.data(),
    };

    auto result_submit =
        _graphics_queue.submit(submit_info, *_frame_in_flight_fence);
    v_assert(result_submit == s_success, "Failed to submit to graphics queue");

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
    v_assert(result_present == s_success, "Failed to present.");
}

#pragma endregion DRAW

} // namespace core
