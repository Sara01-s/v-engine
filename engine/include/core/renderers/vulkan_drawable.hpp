#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef> // offsetof() macro.

namespace core {

struct Vertex {
    glm::vec2 position {};
    glm::vec3 color {};

    static constexpr vk::VertexInputBindingDescription
    binding_description() noexcept {
        constexpr vk::VertexInputBindingDescription binding_description {
            /* ALL the vertices data is packed into a large and contigous array.
               This array is then sent to the GPU for processing.
               But... to the GPU the sentd data is only a bunch of bytes.
               So, we as generous humans should tell the GPU how to interpret the data.
               That's why we use a vertexinputbinding*DESCRIPTION* (this struct).

               Human: "Hey Ms. GPU!, here's the data bound to buffer 0 (binding = 0)"
               GPU:   "Ohh... how nice, let me see..."
               Data:  06 53 B4 34 5C FD 22 24 CD 56 32 D3 6A 87 3B 95 74 05 B4
               GPU:   "what the f#@ is this?"
               Human: "Oh ummm, well let me see... the data also came with a description, it says:
                    
                Dear Ms. GPU,
                For every "sizeof(Vertex)" bytes in the data, there is one vertex.
                Remember to read them as vertices, not instances. 
                Also, don't forget to read the vertices attribute descriptions too.
                You're doing such a great job! Just try not to push yourself 
                too much with all that multitasking! haha.

                Best wishes,
                Mr. Vulkan.
                E-mail sent from CPU Offices.
            */
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex,
        };

        return binding_description;
    }

    static constexpr std::array<vk::VertexInputAttributeDescription, 2>
    attribute_description() noexcept {
        // Position description.
        constexpr vk::VertexInputAttributeDescription position_desc {
            // Location in vertex shader. Accessed with:
            // layout (location = 0) in vec2 position;
            .location = 0,
            .binding = 0,
            // (two 32-bit signed-floats, vec2 in glsl).
            .format = vk::Format::eR32G32Sfloat,
            // Given a pointer to a Vertex, how many bytes should be
            // added to get to the position member.
            .offset = offsetof(Vertex, position),
        };

        constexpr vk::VertexInputAttributeDescription color_desc {
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, color),
        };

        return std::array {position_desc, color_desc};
    }
};

} // namespace core
