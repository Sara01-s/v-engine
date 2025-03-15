#pragma once

#include <GLFW/glfw3.h>
#include <concepts>
#include <type_traits>

#include "log.hpp"
#include "types.hpp"

namespace core {

struct Material {
    std::string texture_file_path {};
    std::string vertex_file_path {};
    std::string vertex_source {};
    std::string fragment_file_path {};
    std::string fragment_source {};
};

template <typename T>
concept RendererAPI =
    requires(T renderer, GLFWwindow* window, Material const& default_material) {
        { renderer.init(window, default_material) } -> std::same_as<void>;
        { renderer.render() } -> std::same_as<void>;
        { renderer.cleanup() } -> std::same_as<void>;
    };

template <RendererAPI GraphicsAPI>
class Renderer {
public:
    explicit Renderer(GraphicsAPI& graphics, Material const& default_material)
        : _graphics {graphics} {
        _init_glfw();

        _graphics.init(
            _window,
            default_material
        ); // Give _graphics the ownership of _window.
    }

    ~Renderer() {
        _graphics.cleanup();
    }

    void
    render() noexcept {
        _graphics.render();
    }

private:
    void
    _init_glfw() {
        if (!glfwInit()) {
            Log::error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        constexpr u32 width {800};
        constexpr u32 height {600};

        _window = glfwCreateWindow(width, height, "Vuwulkan", nullptr, nullptr);

        if (!_window) {
            Log::error("Failed to create window");
        }
    }

private:
    GraphicsAPI& _graphics {};
    GLFWwindow* _window {nullptr};
};

} // namespace core
