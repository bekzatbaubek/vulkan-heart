#ifndef VKH_RENDERER_HPP

#include <sys/types.h>

#include <cstdint>

#include "vkh_math.cpp"

struct Vertex {
    vec3 pos;
    vec3 color;
};

struct Vertex2D {
    vec2 pos;
    vec3 color;
};

struct UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
};

struct Verticies2D {
    uint32_t flags;
    uint32_t count;
    Vertex2D *verticies;
};

#define VKH_RENDERER_HPP
#endif
