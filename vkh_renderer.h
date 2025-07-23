#pragma once

#include <sys/types.h>

#include <cstdint>
#include <vector>

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

    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT debug_messenger;

    VulkanFuncTable func_table;

    VkSwapchainKHR old_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;

    uint64_t MAX_DEVICE_MEMORY_ALLOCATION_SIZE = 1024 * 1024 * 1024;  // 1 GB
    uint64_t MAX_VERTEX_BUFFER_SIZE = 1024 * 1024 * 256;              // 256 MB
    uint64_t MAX_INDEX_BUFFER_SIZE = 1024 * 1024 * 256;               // 256 MB
    uint64_t MAX_INSTANCE_BUFFER_SIZE = 1024 * 1024 * 256;            // 256 MB

    VkBuffer device_memory_buffer;
    VkDeviceMemory device_memory_buffer_memory;
    VkDeviceSize vertex_buffer_offset = 0;
    VkDeviceSize vertex_buffer_size = 0;
    VkDeviceSize index_buffer_offset = MAX_VERTEX_BUFFER_SIZE + 4;
    VkDeviceSize index_buffer_size = 0;
    VkDeviceSize instance_buffer_offset =
        MAX_VERTEX_BUFFER_SIZE + MAX_INDEX_BUFFER_SIZE + 4;
    VkDeviceSize instance_buffer_size = 0;

    uint64_t STAGING_BUFFER_SIZE = 1024 * 1024 * 64;  // 64 MB
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    void* staging_buffer_mapped;

    // TODO: Remove these
    // VkBuffer vertex_buffer;
    // VkDeviceMemory vertex_buffer_memory;
    // VkBuffer index_buffer;
    // VkDeviceMemory index_buffer_memory;
    // VkBuffer instance_buffer;
    // VkDeviceMemory instance_buffer_memory;
    // //

    VkBuffer* uniform_buffers;
    VkDeviceMemory* uniform_buffers_memory;
    void** uniform_buffers_mapped;

    VkCommandPool command_pool;

    // Note: double buffer by default
    uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    VkCommandBuffer* command_buffer;

    VkSemaphore* image_acquire_semaphore;
    VkSemaphore* renderFinishedSemaphore;
    VkFence* in_flight_fence;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet* descriptor_sets;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
};
