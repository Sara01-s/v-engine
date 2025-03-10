#include <core/core.hpp>
#include <core/renderers/vulkan_renderer.hpp>

namespace core {

void
run() {
    VulkanRenderer vk_renderer {};
    Renderer renderer {vk_renderer};
    renderer.render();
}

} // namespace core
