#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 inPosition;

layout(location = 1) in mat4 instanceTransform; // Instance transform (takes 4 attribute slots)
layout(location = 5) in vec3 instanceColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * instanceTransform * vec4(inPosition, 0.0, 1.0);
    fragColor = instanceColor;
}
