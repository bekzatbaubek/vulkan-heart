#include "vulkan/vulkan_core.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "vkh_game.hpp"

#define kilobytes(n) ((n) * 1024)
#define megabytes(n) ((n) * 1024 * 1024)
#define gigabytes(n) ((n) * 1024 * 1024 * 1024)

typedef uint32_t bool32;

time_t getLastModified(const char* path) {
    struct stat attr;
    return stat(path, &attr) == 0 ? attr.st_mtime : 0;
}

struct memory_arena {
    uint8_t* base;
    size_t size;
    size_t used;
};

struct temp_arena {
    memory_arena* parent;
    size_t prev_used;
};

void arena_init(memory_arena* arena, size_t size) {
    arena->base = (uint8_t*)malloc(size);
    arena->size = size;
    arena->used = 0;
}

uint8_t* arena_push(memory_arena* arena, size_t size) {
    assert(arena->used + size <= arena->size);
    uint8_t* result = arena->base + arena->used;
    arena->used += size;
    return result;
}

temp_arena begin_temp_arena(memory_arena* arena) {
    temp_arena temp;
    temp.parent = arena;
    temp.prev_used = arena->used;
    return temp;
}

void end_temp_arena(temp_arena* temp) { temp->parent->used = temp->prev_used; }

struct VulkanContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;

    // Swapchain
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImageView* swapchain_images;

    // Framebuffer
    VkFramebuffer* framebuffers;

    VkCommandPool command_pool;

    const uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    VkCommandBuffer* command_buffer;

    // Sync objects
    VkSemaphore* imageAvailableSemaphore;
    VkSemaphore* renderFinishedSemaphore;
    VkFence* inFlightFence;

    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT debug_messenger;

    // Pipeline
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;
    VkPipeline graphics_pipeline;
};

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << '\n';

    return VK_FALSE;
}

struct queue_indices {
    uint32_t* graphics;
    uint32_t* present;
};

queue_indices get_graphics_and_present_queue_indices(VulkanContext* context,
                                                     memory_arena* arena) {
    queue_indices result = {0};

    result.graphics = (uint32_t*)arena_push(arena, sizeof(result.graphics));
    result.present = (uint32_t*)arena_push(arena, sizeof(result.present));

    temp_arena tmp = begin_temp_arena(arena);

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device,
                                             &queue_family_count, 0);

    VkQueueFamilyProperties* queue_families =
        (VkQueueFamilyProperties*)arena_push(
            arena, sizeof(VkQueueFamilyProperties) * queue_family_count);

    // std::vector<>(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        context->physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *result.graphics = i;
        }
        bool32 present_support;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            context->physical_device, i, context->surface, &present_support);

        if (present_support) {
            *result.present = i;
        }

        if (result.graphics && result.present) {
            break;
        }
    }

    end_temp_arena(&tmp);
    return result;
}

VkPhysicalDevice ChooseDiscreteGPU(VkPhysicalDevice* devices,
                                   uint32_t device_count) {
    assert(device_count > 0);
    return devices[0];
}

struct my_file {
    size_t size;
    char* mem;
};

my_file readfile(const char* path, memory_arena* arena) {
    FILE* f = fopen(path, "rb");
    assert(f);
    my_file mf;

    struct stat attr;
    if (!stat(path, &attr)) {
        uint32_t file_size = attr.st_size;
        uint32_t padding = 0;

        if (file_size % 4 != 0) {
            std::cerr << "File size is not a multiple of 4\n";
            padding = 4 - (file_size % 4);
        }

        char* file_in_mem =
            (char*)arena_push(arena, sizeof(char) * file_size + padding);

        size_t ret_code = fread(file_in_mem, sizeof(char), file_size, f);
        assert(ret_code == file_size);

        mf.size = file_size + padding;
        mf.mem = file_in_mem;
    }

    return mf;
}

void CreateGraphicsPipeline(VulkanContext* context, memory_arena* arena) {
    temp_arena tmp = begin_temp_arena(arena);

    my_file vert_shader_mf = readfile("./shaders/heart.vert.spv", arena);
    my_file frag_shader_mf = readfile("./shaders/heart.frag.spv", arena);

    VkShaderModule vert_shader_module;
    VkShaderModule frag_shader_module;
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = vert_shader_mf.size;
        createInfo.pCode = (uint32_t*)vert_shader_mf.mem;
        VkResult res = vkCreateShaderModule(context->device, &createInfo,
                                            nullptr, &vert_shader_module);
        assert(res == VK_SUCCESS);
    }
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = frag_shader_mf.size;
        createInfo.pCode = (uint32_t*)frag_shader_mf.mem;
        VkResult res = vkCreateShaderModule(context->device, &createInfo,
                                            nullptr, &frag_shader_module);
        assert(res == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;

    vertShaderStageInfo.module = vert_shader_module;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = frag_shader_module;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)context->swapchain_extent.width;
    viewport.height = (float)context->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // multisampling.minSampleShading = 0.2f;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkResult res = vkCreatePipelineLayout(context->device, &pipelineLayoutInfo,
                                          0, &context->pipeline_layout);
    assert(res == VK_SUCCESS);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;  // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = context->pipeline_layout;
    pipelineInfo.renderPass = context->render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // Optional
    pipelineInfo.basePipelineIndex = -1;               // Optional

    res = vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1,
                                    &pipelineInfo, nullptr,
                                    &context->graphics_pipeline);
    assert(res == VK_SUCCESS);

    vkDestroyShaderModule(context->device, vert_shader_module, 0);
    vkDestroyShaderModule(context->device, frag_shader_module, 0);

    end_temp_arena(&tmp);
}

void CreateSwapchain(VulkanContext* context, GLFWwindow* window,
                     memory_arena* parent_arena) {
    // Check whether the device meets requirements
    VkSurfaceFormatKHR surfaceFormat;
    VkSurfaceCapabilitiesKHR capabilities;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface, &capabilities);

    std::cerr << "Surface capabilities: minImageCount = "
              << capabilities.minImageCount
              << ", maxImageCount = " << capabilities.maxImageCount
              << std::endl;

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface, &formatCount, 0);

    if (formatCount) {
        VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)arena_push(
            parent_arena, formatCount * sizeof(VkSurfaceFormatKHR));

        vkGetPhysicalDeviceSurfaceFormatsKHR(
            context->physical_device, context->surface, &formatCount, formats);

        for (uint32_t i = 0; i < formatCount; ++i) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                std::cerr << "Found suitable format\n";
                surfaceFormat = formats[i];
                break;
            }
        }
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        context->physical_device, context->surface, &presentModeCount, 0);

    if (presentModeCount) {
        VkPresentModeKHR* presentModes = (VkPresentModeKHR*)arena_push(
            parent_arena, presentModeCount * sizeof(VkPresentModeKHR));

        vkGetPhysicalDeviceSurfacePresentModesKHR(
            context->physical_device, context->surface, &presentModeCount,
            presentModes);

        for (uint32_t i = 0; i < presentModeCount; ++i) {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                std::cerr << "Found suitable present mode\n";
                presentMode = presentModes[i];
                break;
            }
        }
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {(uint32_t)(width), (uint32_t)(height)};

    if (actualExtent.width > capabilities.maxImageExtent.width &&
        actualExtent.height > capabilities.maxImageExtent.height) {
        actualExtent.width = capabilities.maxImageExtent.width;
        actualExtent.height = capabilities.maxImageExtent.height;
    } else if (actualExtent.width < capabilities.minImageExtent.width ||
               actualExtent.height < capabilities.minImageExtent.height) {
        actualExtent.width = capabilities.minImageExtent.width;
        actualExtent.height = capabilities.minImageExtent.height;
    }

    extent = actualExtent;

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context->surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    queue_indices q_indices =
        get_graphics_and_present_queue_indices(context, parent_arena);

    uint32_t queueFamilyIndices[] = {*q_indices.graphics, *q_indices.present};

    if (*q_indices.graphics != *q_indices.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult res = vkCreateSwapchainKHR(context->device, &createInfo, 0,
                                        &context->swapchain);
    assert(res == VK_SUCCESS);

    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            0);
    VkImage* swapchain_images =
        (VkImage*)arena_push(parent_arena, imageCount * sizeof(VkImage));

    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            swapchain_images);

    context->swapchain_image_count = imageCount;

    context->swapchain_images = (VkImageView*)arena_push(
        parent_arena, imageCount * sizeof(VkImageView));

    for (int i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchain_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        res = vkCreateImageView(context->device, &viewInfo, nullptr,
                                &context->swapchain_images[i]);
    }

    context->swapchain_format = surfaceFormat.format;
    context->swapchain_extent = extent;
}

void CreateRenderPass(VulkanContext* context, memory_arena* arena) {
    VkAttachmentDescription color_attachment{};

    color_attachment.format = context->swapchain_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &color_attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(context->device, &renderPassInfo, nullptr,
                                      &context->render_pass);

    assert(res == VK_SUCCESS);
}

void CreateFramebuffers(VulkanContext* context, memory_arena* arena) {
    context->framebuffers = (VkFramebuffer*)arena_push(
        arena, sizeof(VkFramebuffer) * context->swapchain_image_count);

    for (uint32_t i = 0; i < context->swapchain_image_count; i++) {
        VkImageView attachments[] = {context->swapchain_images[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = context->render_pass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = context->swapchain_extent.width;
        framebufferInfo.height = context->swapchain_extent.height;
        framebufferInfo.layers = 1;

        VkResult res = vkCreateFramebuffer(context->device, &framebufferInfo,
                                           nullptr, &context->framebuffers[i]);
        assert(res == VK_SUCCESS);
    }
}

void CreateCommandPool(VulkanContext* context, memory_arena* arena) {
    queue_indices q_idxs =
        get_graphics_and_present_queue_indices(context, arena);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *q_idxs.graphics;

    VkResult res = vkCreateCommandPool(context->device, &poolInfo, nullptr,
                                       &context->command_pool);

    assert(res == VK_SUCCESS);
}

void CreateCommandBuffer(VulkanContext* context, memory_arena* arena) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = context->MAX_FRAMES_IN_FLIGHT;

    context->command_buffer = (VkCommandBuffer*)arena_push(
        arena, sizeof(VkCommandBuffer) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult res = vkAllocateCommandBuffers(context->device, &allocInfo,
                                                &context->command_buffer[i]);
        assert(res == VK_SUCCESS);
    }
}

void RecordCommandBuffer(VulkanContext* context, uint32_t image_index,
                         memory_arena* arena, uint32_t current_frame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult res = vkBeginCommandBuffer(context->command_buffer[current_frame],
                                        &beginInfo);
    assert(res == VK_SUCCESS);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = context->render_pass;
    renderPassInfo.framebuffer = context->framebuffers[image_index];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = context->swapchain_extent;

    // Test green clear
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(context->command_buffer[current_frame],
                         &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(context->command_buffer[current_frame],
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      context->graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)context->swapchain_extent.width;
    viewport.height = (float)context->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context->command_buffer[current_frame], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context->swapchain_extent;
    vkCmdSetScissor(context->command_buffer[current_frame], 0, 1, &scissor);

    vkCmdDraw(context->command_buffer[current_frame], 3, 1, 0, 0);

    vkCmdEndRenderPass(context->command_buffer[current_frame]);

    res = vkEndCommandBuffer(context->command_buffer[current_frame]);
    assert(res == VK_SUCCESS);
}

void CreateSyncObjects(VulkanContext* context, memory_arena* arena) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Fence is signalled first, so the drawFrame call can wait for it
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    context->imageAvailableSemaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->renderFinishedSemaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->inFlightFence = (VkFence*)arena_push(
        arena, sizeof(VkFence) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->imageAvailableSemaphore[i]);
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->renderFinishedSemaphore[i]);
        vkCreateFence(context->device, &fenceInfo, nullptr,
                      &context->inFlightFence[i]);
    }
}

void renderer_init(VulkanContext* context, GLFWwindow* window,
                   memory_arena* renderer_arena) {
    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset",
    };

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &appInfo;

#ifndef NDEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;

    instance_info.enabledLayerCount =
        sizeof(validation_layers) / sizeof(const char*);
    instance_info.ppEnabledLayerNames = validation_layers;
    instance_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

#else
    instance_info.enabledLayerCount = 0;
    instance_info.pNext = 0;
#endif

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions,
                                        glfwExtensions + glfwExtensionCount);

#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    instance_info.enabledExtensionCount = extensions.size();
    instance_info.ppEnabledExtensionNames = extensions.data();

    instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkResult res = vkCreateInstance(&instance_info, 0, &context->instance);
    assert(res == VK_SUCCESS);

    auto vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkCreateDebugUtilsMessengerEXT");
    assert(vkCreateDebugUtilsMessengerEXT);
    vkCreateDebugUtilsMessengerEXT(context->instance, &debugCreateInfo, nullptr,
                                   &context->debug_messenger);

    // 2. Physical Device
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &device_count, 0);
    VkPhysicalDevice* devices = (VkPhysicalDevice*)arena_push(
        renderer_arena, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(context->instance, &device_count, devices);
    context->physical_device = ChooseDiscreteGPU(devices, device_count);

    glfwCreateWindowSurface(context->instance, window, nullptr,
                            &context->surface);

    // 3. Logical Device
    //
    queue_indices q_indices =
        get_graphics_and_present_queue_indices(context, renderer_arena);

    uint32_t number_of_unique_queues = 1;
    if (*q_indices.graphics != *q_indices.present) {
        number_of_unique_queues = 2;
    }

    temp_arena tmp = begin_temp_arena(renderer_arena);

    VkDeviceQueueCreateInfo* queueCreateInfos =
        (VkDeviceQueueCreateInfo*)arena_push(
            renderer_arena,
            sizeof(VkDeviceQueueCreateInfo) * number_of_unique_queues);

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = *q_indices.graphics;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queue_priority;
    queueCreateInfos[0] = queueCreateInfo;

    if (number_of_unique_queues == 2) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = *q_indices.present;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queue_priority;
        queueCreateInfos[1] = queueCreateInfo;
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;

    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = number_of_unique_queues;
    device_info.pQueueCreateInfos = queueCreateInfos;

    device_info.pEnabledFeatures = &deviceFeatures;

#ifndef NDEBUG
    device_info.enabledLayerCount = 1;
    device_info.ppEnabledLayerNames = validation_layers;
#endif

    device_info.enabledExtensionCount = 2;
    device_info.ppEnabledExtensionNames = device_extensions;

    res = vkCreateDevice(context->physical_device, &device_info, 0,
                         &context->device);

    vkGetDeviceQueue(context->device, *q_indices.graphics, 0,
                     &context->graphics_queue);
    vkGetDeviceQueue(context->device, *q_indices.present, 0,
                     &context->present_queue);

    end_temp_arena(&tmp);
    assert(res == VK_SUCCESS);

    // 4. Swapchain
    CreateSwapchain(context, window, renderer_arena);

    CreateRenderPass(context, renderer_arena);

    CreateGraphicsPipeline(context, renderer_arena);

    CreateFramebuffers(context, renderer_arena);

    CreateCommandPool(context, renderer_arena);
    CreateCommandBuffer(context, renderer_arena);

    CreateSyncObjects(context, renderer_arena);
}

void drawFrame(VulkanContext* context, memory_arena* arena) {
    static uint32_t currentFrame = 0;

    vkWaitForFences(context->device, 1, &context->inFlightFence[currentFrame],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(context->device, 1, &context->inFlightFence[currentFrame]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(context->device, context->swapchain, UINT64_MAX,
                          context->imageAvailableSemaphore[currentFrame],
                          VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(context->command_buffer[currentFrame], 0);
    RecordCommandBuffer(context, imageIndex, arena, currentFrame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {
        context->imageAvailableSemaphore[currentFrame]};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context->command_buffer[currentFrame];

    VkSemaphore signalSemaphores[] = {
        context->renderFinishedSemaphore[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult res = vkQueueSubmit(context->graphics_queue, 1, &submitInfo,
                                 context->inFlightFence[currentFrame]);
    assert(res == VK_SUCCESS);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {context->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(context->present_queue, &presentInfo);

    currentFrame = (currentFrame + 1) % context->MAX_FRAMES_IN_FLIGHT;
}

int main(int argc, char* argv[]) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan Heart", 0, 0);
    assert(window);

    const char* sourcePath = "./build/lib.so";
    game_update_t gameUpdateAndRender = 0;

    void* so_handle = dlopen(sourcePath, RTLD_NOW);
    gameUpdateAndRender =
        (game_update_t)dlsym(so_handle, "game_update_and_render");
    time_t lastModified = getLastModified(sourcePath);

    memory_arena vk_arena = {0};
    arena_init(&vk_arena, megabytes(128));

    VulkanContext context = {0};
    renderer_init(&context, window, &vk_arena);

    game_memory state;
    state.permanent_store_size = megabytes((uint64_t)256);
    state.permanent_store = malloc(state.permanent_store_size);

    state.transient_store_size = gigabytes((uint64_t)2);
    state.transient_store = malloc(state.transient_store_size);

    uint64_t timer_frequency = glfwGetTimerFrequency();

    uint32_t target_FPS = 30;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        uint64_t start_time = glfwGetTimerValue();
        time_t currentModified = getLastModified(sourcePath);
        if (currentModified > lastModified) {
            lastModified = currentModified;

            dlclose(so_handle);
            so_handle = dlopen(sourcePath, RTLD_NOW);
            gameUpdateAndRender =
                (game_update_t)dlsym(so_handle, "game_update_and_render");
        }

        gameUpdateAndRender(&state);
        drawFrame(&context, &vk_arena);

        uint64_t end_time = glfwGetTimerValue();
        double time_elapsed = ((double)end_time / timer_frequency) -
                              ((double)start_time / timer_frequency);
        uint64_t sleepTime = (1000.0 / target_FPS) - (uint64_t)time_elapsed;
        // std::cerr << "Elapsed time: " << time_elapsed
        //           << "ms, Sleep time: " << sleepTime << "ms\n";
        // std::cerr << "FPS: " << 1000.0 / (time_elapsed + sleepTime) <<
        // "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
    }
    vkDeviceWaitIdle(context.device);

    dlclose(so_handle);

    return 0;
}
