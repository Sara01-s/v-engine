#include <core/asset_database.hpp>
#include <core/core.hpp>
#include <core/renderers/vulkan_renderer.hpp>

namespace core {

void
run() {
    std::string const texture_file_path =
        AssetDatabase::resolve("textures/tex_viking_room.png");
    std::string const vert_file_path =
        AssetDatabase::resolve("shaders/sh_default.vert").string();
    std::string const frag_file_path =
        AssetDatabase::resolve("shaders/sh_default.vert").string();
    std::string const model_file_path =
        AssetDatabase::resolve("models/model_viking_room.obj");

    std::string vert_shader_src =
        AssetDatabase::read_asset_file("shaders/sh_default.vert");
    std::string frag_shader_src =
        AssetDatabase::read_asset_file("shaders/sh_default.frag");

    RenderInfo const default_material {
        .texture_file_path = texture_file_path,
        .model_file_path = model_file_path,
        .vertex_file_path = vert_file_path,
        .vertex_source = vert_shader_src,
        .fragment_file_path = frag_file_path,
        .fragment_source = frag_shader_src,
    };

    VulkanRenderer vk_renderer {};
    Renderer renderer {vk_renderer, default_material};
    renderer.render();
}

} // namespace core
