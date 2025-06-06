#pragma once

#include <sys/types.h>

#include "vkh_math.h"
#include "vulkan/vulkan_core.h"

struct Vertex {
    vec3 pos;
};

struct Vertex2D {
    vec2 pos;
};

struct UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
};

struct InstanceData {
    mat4 transform;
    vec3 color;
};

struct queue_indices {
    uint32_t* graphics;
    uint32_t* present;
};

struct VulkanContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;

    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT debug_messenger;

    // Swapchain
    VkSwapchainKHR old_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;

    // Buffers
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;
    VkBuffer instance_buffer;
    VkDeviceMemory instance_buffer_memory;

    VkBuffer* uniform_buffers;
    VkDeviceMemory* uniform_buffers_memory;
    void** uniform_buffers_mapped;

    VkCommandPool command_pool;

    uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    VkImageView* swapchain_images;
    VkCommandBuffer* command_buffer;
    VkFramebuffer* framebuffers;
    // Sync objects
    VkSemaphore* image_available_semaphore;
    VkSemaphore* renderFinishedSemaphore;
    VkFence* in_flight_fence;

    // Pipeline
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet* descriptor_sets;
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;
    VkPipeline graphics_pipeline;

    VkPhysicalDeviceFeatures device_features;
    VkPhysicalDeviceProperties device_properties;
};
