#pragma once

#include <vector>

#include <SDL3/SDL_stdinc.h>

#include "vkh_math.h"
#include <vulkan/vulkan.h>

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

struct VulkanSwapchainResources {
    VkSwapchainKHR swapchain;
    VkSwapchainKHR old_swapchain = VK_NULL_HANDLE;
    uint32_t swapchain_image_count;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
};

struct VulkanFuncTable {
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = 0;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = 0;
    PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR = 0;
};

struct VulkanContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;

    VkPhysicalDeviceProperties2 physical_device_properties2;

    int WindowDrawableAreaWidth;
    int WindowDrawableAreaHeight;
    float WindowPixelDensity;
    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT debug_messenger;

    VulkanFuncTable func_table;

    VkSwapchainKHR old_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;

    // Note: double buffer by default
    // One image presented and the other is being rendered to
    uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VkSemaphore* image_acquire_semaphore;
    VkSemaphore* render_finished_semaphore;
    VkFence* in_flight_fence;

    const uint64_t MAX_DEVICE_MEMORY_ALLOCATION_SIZE =
        1024 * 1024 * 1024;                                       // 1 GB
    const uint64_t MAX_VERTEX_BUFFER_SIZE = 1024 * 1024 * 256;    // 256 MB
    const uint64_t MAX_INDEX_BUFFER_SIZE = 1024 * 1024 * 256;     // 256 MB
    const uint64_t MAX_INSTANCE_BUFFER_SIZE = 1024 * 1024 * 256;  // 256 MB

    VkBuffer device_memory_buffer;
    VkDeviceMemory device_memory_buffer_memory;
    VkDeviceSize vertex_buffer_offset = 0;
    VkDeviceSize vertex_buffer_size = 0;
    VkDeviceSize index_buffer_offset = MAX_VERTEX_BUFFER_SIZE + 4;
    VkDeviceSize index_buffer_size = 0;
    VkDeviceSize instance_buffer_offset =
        MAX_VERTEX_BUFFER_SIZE + MAX_INDEX_BUFFER_SIZE + 4;
    VkDeviceSize instance_buffer_size = 0;

    const uint64_t STAGING_BUFFER_SIZE = 1024 * 1024 * 64;  // 64 MB
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    void* staging_buffer_mapped;

    VkBuffer* uniform_buffers;
    VkDeviceMemory* uniform_buffers_memory;
    void** uniform_buffers_mapped;

    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet* descriptor_sets;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
};
