#ifndef VKH_RENDERER_HPP

#include <sys/types.h>

#include <array>
#include <cstdint>

#include "vulkan/vulkan.h"

struct vec2 {
    float x, y;
};

struct vec3 {
    float x, y, z;
};

struct Vertex {
    vec3 pos;
    vec3 color;
};

struct Vertex2D {
    vec2 pos;
    vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};

        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex2D);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2>
    getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2>
            attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex2D, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex2D, color);

        return attributeDescriptions;
    }
};

struct Verticies2D {
    uint32_t flags;
    uint32_t count;
    Vertex2D *verticies;
};

#define VKH_RENDERER_HPP
#endif
