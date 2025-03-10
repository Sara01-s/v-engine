#pragma once

#include <GLFW/glfw3.h>

#include "log.hpp"
#include "types.hpp"

namespace core {

template <typename T>
concept RendererAPI = requires(T renderer, GLFWwindow* window) {
    { renderer.init(window) } -> std::same_as<void>;
    { renderer.render() } -> std::same_as<void>;
    { renderer.cleanup() } -> std::same_as<void>;
};

template <RendererAPI GfxAPI>
class Renderer {
public:
    explicit Renderer(GfxAPI& gfx) : _gfx {gfx} {
        _init_glfw();
        _gfx.init(_window); // give gfx the ownership of _window.
    }

    ~Renderer() {
        _gfx.cleanup();
    }

    void
    render() {
        _gfx.render();
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

        _window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);

        if (!_window) {
            Log::error("Failed to create window");
        }
    }

private:
    GfxAPI& _gfx {};
    GLFWwindow* _window {nullptr};
};

} // namespace core
