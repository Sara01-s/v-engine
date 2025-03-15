#include <core/core.hpp>
#include <core/renderers/vulkan_renderer.hpp>

namespace core {

void
run() {
    std::string const vert_file_path =
        AssetDatabase::resolve("shaders/basic.vert").string();
    std::string const frag_file_path =
        AssetDatabase::resolve("shaders/basic.vert").string();

    auto vert_shader_src = AssetDatabase::read_asset_file("shaders/basic.vert");
    auto frag_shader_src = AssetDatabase::read_asset_file("shaders/basic.frag");

    DefaultShaderSrc const default_shader {
        .vertex_file_path = vert_file_path,
        .vertex_src = vert_shader_src,
        .fragment_file_path = frag_file_path,
        .fragment_src = frag_shader_src,
    };

    VulkanRenderer vk_renderer {};
    Renderer renderer {vk_renderer, default_shader};
    renderer.render();
}

} // namespace core
