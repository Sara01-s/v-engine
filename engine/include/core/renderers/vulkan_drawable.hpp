#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef> // offsetof() macro.

namespace core {

struct UniformBufferObject {
    // Vulkan expects the data in the UBO to be aligned in memory
    // in a specific way, for example:
    /* 
        Scalars have to be aligned by N = 4 bytes (given 32-bit floats).
        A vec2 must be aligned by 2N = 8 bytes.
        A vec3 or vec4 must be aligned by 4N = 16 bytes.
        A nested structure must be aligned by the base alignment of its members
            rounded up to a multiple of 16.
        A mat4 matrix must have the same alignmest as a vec4.

        Full list of alighment requirements:
        src: https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-resources-layout
        
        How to pseudo-automate this:
        src: https://docs.vulkan.org/tutorial/latest/05_Uniform_buffers/01_Descriptor_pool_and_sets.html#:~:text=Luckily%20there%20is%20a%20way%20to%20not%20have%20to%20think%20about%20these%20alignment%20requirements%20most%20of%20the%20time.%20We%20can%20define%20GLM_FORCE_DEFAULT_ALIGNED_GENTYPES%20right%20before%20including%20GLM:
    */
    alignas(16) glm::mat4 model {};
    alignas(16) glm::mat4 view {};
    alignas(16) glm::mat4 projection {};
};

struct Vertex {
    glm::vec2 position {};
    glm::vec3 color {};
    glm::vec2 tex_coord {};

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

    static constexpr std::array<vk::VertexInputAttributeDescription, 3>
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

        // Color description.
        constexpr vk::VertexInputAttributeDescription color_desc {
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, color),
        };

        // Texture coordinates description.
        constexpr vk::VertexInputAttributeDescription tex_coords_desc {
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, tex_coord),
        };

        return std::array {position_desc, color_desc, tex_coords_desc};
    }
};

} // namespace core
