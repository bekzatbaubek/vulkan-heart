#include "vkh_renderer.h"

#include <GLFW/glfw3.h>
#include <sys/stat.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "vkh_game.h"
#include "vkh_memory.h"

#define ArrayCount(x) (sizeof(x) / sizeof((x)[0]))

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << '\n';

    return VK_FALSE;
}

queue_indices get_graphics_and_present_queue_indices(VulkanContext* context,
                                                     MemoryArena* arena) {
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

    vkGetPhysicalDeviceQueueFamilyProperties(
        context->physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *result.graphics = i;
        }
        uint32_t present_support;
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

VkPhysicalDevice ChooseDiscreteGPU(VulkanContext* context,
                                   VkPhysicalDevice* devices,
                                   uint32_t device_count) {
    assert(device_count > 0);

    for (int i = 0; i < device_count; i++) {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(devices[i], &device_properties);

        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(devices[i], &device_features);

        if (device_properties.deviceType ==
                VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
            device_properties.deviceType ==
                VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            std::cerr << "GPU found\n"
                      << "GPU name: " << device_properties.deviceName << '\n';

            return devices[i];
        }
    }

    return devices[0];
}

struct my_file {
    size_t size;
    char* mem;
};

my_file readfile(const char* path, MemoryArena* arena) {
#ifdef _WIN64
    FILE* f;
    fopen_s(&f, path, "rb");
#else
    FILE* f = fopen(path, "rb");
#endif
    my_file mf = {0, 0};

    struct stat attr;
    int result = stat(path, &attr);
    if (result == 0) {
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
    fclose(f);

    return mf;
}

void CreateDescriptorSetLayout(VulkanContext* context, MemoryArena* arena) {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;

    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    vkCreateDescriptorSetLayout(context->device, &layoutInfo, nullptr,
                                &context->descriptor_set_layout);
}

void CreateGraphicsPipeline(VulkanContext* context, MemoryArena* arena) {
    temp_arena tmp = begin_temp_arena(arena);

    my_file vert_shader_mf = readfile("shaders/heart.vert.spv", arena);
    my_file frag_shader_mf = readfile("shaders/heart.frag.spv", arena);

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
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::array<VkVertexInputBindingDescription, 2> bindingDescriptions;

    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex2D);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions;

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex2D, pos);

    for (int i = 0; i < 4; i++) {
        attributeDescriptions[1 + i].binding = 1;
        attributeDescriptions[1 + i].location = 1 + i;
        attributeDescriptions[1 + i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1 + i].offset = i * sizeof(float) * 4;
    }

    attributeDescriptions[5].binding = 1;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[5].offset = sizeof(float) * 16;

    vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
    vertexInputInfo.vertexAttributeDescriptionCount =
        attributeDescriptions.size();
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context->descriptor_set_layout;

    VkResult res = vkCreatePipelineLayout(context->device, &pipelineLayoutInfo,
                                          0, &context->pipeline_layout);
    assert(res == VK_SUCCESS);

    VkPipelineRenderingCreateInfoKHR pipeline_create{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    pipeline_create.pNext = VK_NULL_HANDLE;
    pipeline_create.colorAttachmentCount = 1;
    pipeline_create.pColorAttachmentFormats = &context->swapchain_format;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipeline_create;

    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = context->pipeline_layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    res = vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1,
                                    &pipelineInfo, nullptr,
                                    &context->graphics_pipeline);
    assert(res == VK_SUCCESS);

    vkDestroyShaderModule(context->device, vert_shader_module, 0);
    vkDestroyShaderModule(context->device, frag_shader_module, 0);

    end_temp_arena(&tmp);
}

void CreateSwapchain(VulkanContext* context, MemoryArena* parent_arena) {
    VkSurfaceFormatKHR surfaceFormat = {
        VK_FORMAT_UNDEFINED,
    };
    VkSurfaceCapabilitiesKHR capabilities;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface, &capabilities);

    std::cerr << "Surface capabilities: minImageCount = "
              << capabilities.minImageCount
              << ", maxImageCount = " << capabilities.maxImageCount << '\n';

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    std::cerr << "Engine Image Count: " << imageCount << '\n';

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface, &formatCount, 0);

    temp_arena tmp = begin_temp_arena(parent_arena);

    if (formatCount) {
        VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)arena_push(
            parent_arena, formatCount * sizeof(VkSurfaceFormatKHR));

        vkGetPhysicalDeviceSurfaceFormatsKHR(
            context->physical_device, context->surface, &formatCount, formats);

        for (uint32_t i = 0; i < formatCount; ++i) {
            if (formats[i].colorSpace ==
                VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT) {
                std::cerr << "Found format: "
                          << string_VkFormat(formats[i].format)
                          << " and colorspace: "
                          << string_VkColorSpaceKHR(formats[i].colorSpace)
                          << '\n';
            }
        }
        for (uint32_t i = 0; i < formatCount; ++i) {
            if (formats[i].format == VK_FORMAT_R16G16B16A16_SFLOAT &&
                formats[i].colorSpace ==
                    VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT) {
                std::cerr << "Found suitable format"
                          << string_VkFormat(formats[i].format)
                          << " and colorspace: "
                          << string_VkColorSpaceKHR(formats[i].colorSpace)
                          << '\n';
                surfaceFormat = formats[i];
                break;
            }
        }

        if (surfaceFormat.format == VK_FORMAT_UNDEFINED) {
            surfaceFormat.format = VK_FORMAT_B8G8R8A8_SRGB;
            surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            std::cerr << "No suitable format found, using default:"
                      << string_VkFormat(surfaceFormat.format)
                      << " colorspace: "
                      << string_VkColorSpaceKHR(surfaceFormat.colorSpace)
                      << '\n';
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
                std::cerr << "Found mailbox present mode\n";
                presentMode = presentModes[i];
                break;
            }
            if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                std::cerr << "Found immediate present mode\n";
            }
        }
    }

    VkExtent2D actualExtent = capabilities.currentExtent;

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
        createInfo.queueFamilyIndexCount = 1;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    createInfo.oldSwapchain = context->old_swapchain;

    VkResult res = vkCreateSwapchainKHR(context->device, &createInfo, 0,
                                        &context->swapchain);

    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            0);

    end_temp_arena(&tmp);

    context->swapchain_images =
        (VkImage*)arena_push(parent_arena, imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            context->swapchain_images);

    context->swapchain_image_count = imageCount;

    if (context->old_swapchain) {
    } else {
        context->swapchain_image_views = (VkImageView*)arena_push(
            parent_arena, imageCount * sizeof(VkImageView));
    }

    for (int i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = context->swapchain_images[i];
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

        VkResult res = vkCreateImageView(context->device, &viewInfo, nullptr,
                                         &context->swapchain_image_views[i]);
    }

    context->swapchain_format = surfaceFormat.format;
    context->swapchain_extent = extent;

    std::cerr << "Swapchain extent: " << context->swapchain_extent.width << ", "
              << context->swapchain_extent.height << '\n';
}

void CreateCommandPool(VulkanContext* context, MemoryArena* arena) {
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

uint32_t findMemoryType(VkPhysicalDevice phyical_devices, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(phyical_devices, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }

    InvalidCodePath;
    return -1;
}

void CopyBuffer(VulkanContext* context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context->command_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(context->device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(context->graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->graphics_queue);

    vkFreeCommandBuffers(context->device, context->command_pool, 1,
                         &commandBuffer);
}

void CreateBuffer(VulkanContext* context, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(context->device, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        context->physical_device, memRequirements.memoryTypeBits, properties);

    vkAllocateMemory(context->device, &allocInfo, nullptr, &bufferMemory);

    vkBindBufferMemory(context->device, buffer, bufferMemory, 0);
}

void CreateVertexBuffer(VulkanContext* context) {
    const std::vector<Vertex2D> vertices = {
        {1.0f, 0.0f},
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
    };

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(context, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(context->device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(context->device, stagingBufferMemory);

    CreateBuffer(
        context, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, context->vertex_buffer,
        context->vertex_buffer_memory);

    CopyBuffer(context, stagingBuffer, context->vertex_buffer, bufferSize);
}

void CreateUniformBuffers(VulkanContext* context, MemoryArena* arena) {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    context->uniform_buffers = (VkBuffer*)arena_push(
        arena, sizeof(VkBuffer) * context->MAX_FRAMES_IN_FLIGHT);

    context->uniform_buffers_memory = (VkDeviceMemory*)arena_push(
        arena, sizeof(VkDeviceMemory) * context->MAX_FRAMES_IN_FLIGHT);

    context->uniform_buffers_mapped = (void**)arena_push(
        arena, sizeof(void*) * context->MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        CreateBuffer(context, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     context->uniform_buffers[i],
                     context->uniform_buffers_memory[i]);

        vkMapMemory(context->device, context->uniform_buffers_memory[i], 0,
                    bufferSize, 0, &context->uniform_buffers_mapped[i]);
    }
}

void CreateDescriptorPool(VulkanContext* context) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = context->MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = context->MAX_FRAMES_IN_FLIGHT;

    vkCreateDescriptorPool(context->device, &poolInfo, nullptr,
                           &context->descriptor_pool);
}

void CreateDescriptorSets(VulkanContext* context, MemoryArena* arena) {
    VkDescriptorSetLayout* layouts = (VkDescriptorSetLayout*)arena_push(
        arena, sizeof(VkDescriptorSetLayout) * context->MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = context->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context->descriptor_pool;
    allocInfo.descriptorSetCount = context->MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts;

    context->descriptor_sets = (VkDescriptorSet*)arena_push(
        arena, sizeof(VkDescriptorSet) * context->MAX_FRAMES_IN_FLIGHT);

    vkAllocateDescriptorSets(context->device, &allocInfo,
                             context->descriptor_sets);

    for (size_t i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = context->uniform_buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = context->descriptor_sets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;

        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;

        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr;
        descriptorWrite.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(context->device, 1, &descriptorWrite, 0,
                               nullptr);
    }
}

void CreateInstanceBuffer(VulkanContext* context) {
    std::vector<InstanceData> instances;
    instances.reserve(10 * 10);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            InstanceData instance;

            float x = i * 1.0f;
            float y = j * 1.0f;

            instance.transform =
                multiply(translate(x, y, 0.0f), scale(200.0f, 190.0f, 1.0f));

            instance.color = {(float)i / 5.0f, (float)j / 5.0f, 0.5f};

            instances.emplace_back(instance);
        }
    }

    VkDeviceSize bufferSize = sizeof(instances[0]) * instances.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(context, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(context->device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, instances.data(), (size_t)bufferSize);
    vkUnmapMemory(context->device, stagingBufferMemory);

    CreateBuffer(
        context, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, context->instance_buffer,
        context->instance_buffer_memory);

    CopyBuffer(context, stagingBuffer, context->instance_buffer, bufferSize);

    vkDestroyBuffer(context->device, stagingBuffer, nullptr);
    vkFreeMemory(context->device, stagingBufferMemory, nullptr);
}

void CreateIndexBuffer(VulkanContext* context) {
    const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(context, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(context->device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(context->device, stagingBufferMemory);

    CreateBuffer(
        context, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, context->index_buffer,
        context->index_buffer_memory);

    CopyBuffer(context, stagingBuffer, context->index_buffer, bufferSize);

    vkDestroyBuffer(context->device, stagingBuffer, 0);
    vkFreeMemory(context->device, stagingBufferMemory, 0);
}

void CreateCommandBuffers(VulkanContext* context, MemoryArena* arena) {
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

void TransitionImageLayout(VulkanContext* context, VkCommandBuffer cmd,
                           VkImage image, VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           VkAccessFlags2 srcAccessMask,
                           VkAccessFlags2 dstAccessMask,
                           VkPipelineStageFlags2 srcStageMask,
                           VkPipelineStageFlags2 dstStageMask) {
    VkImageMemoryBarrier2KHR image_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,

        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,

        .oldLayout = oldLayout,
        .newLayout = newLayout,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = image,

        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,

                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo dependency_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &image_barrier,
    };

    context->func_table.vkCmdPipelineBarrier2KHR(cmd, &dependency_info);
}

void RecordCommandBuffer(VulkanContext* context, uint32_t image_index,
                         MemoryArena* arena, uint32_t current_frame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult res = vkBeginCommandBuffer(context->command_buffer[current_frame],
                                        &beginInfo);
    assert(res == VK_SUCCESS);

    TransitionImageLayout(context, context->command_buffer[current_frame],
                          context->swapchain_images[image_index],
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue clear_value = {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}},
    };

    VkRenderingAttachmentInfoKHR color_attachment_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .pNext = VK_NULL_HANDLE,
        .imageView = context->swapchain_image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_value,
    };

    VkRect2D render_area = VkRect2D{
        VkOffset2D{},
        VkExtent2D{context->swapchain_extent.width,
                   context->swapchain_extent.height},
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .pNext = VK_NULL_HANDLE,
        .flags = 0,
        .renderArea = render_area,
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_info,
        .pDepthAttachment = VK_NULL_HANDLE,
        .pStencilAttachment = VK_NULL_HANDLE,
    };

    context->func_table.vkCmdBeginRenderingKHR(
        context->command_buffer[current_frame], &renderingInfo);

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

    VkBuffer vertexBuffers[] = {context->vertex_buffer,
                                context->instance_buffer};
    VkDeviceSize offsets[] = {0, 0};

    vkCmdBindVertexBuffers(context->command_buffer[current_frame], 0, 2,
                           vertexBuffers, offsets);
    vkCmdBindIndexBuffer(context->command_buffer[current_frame],
                         context->index_buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        context->command_buffer[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        context->pipeline_layout, 0, 1,
        &context->descriptor_sets[current_frame], 0, nullptr);

    vkCmdDrawIndexed(context->command_buffer[current_frame], 6, 200 * 200, 0, 0,
                     0);

    context->func_table.vkCmdEndRenderingKHR(
        context->command_buffer[current_frame]);

    //
    TransitionImageLayout(context, context->command_buffer[current_frame],
                          context->swapchain_images[image_index],
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);

    res = vkEndCommandBuffer(context->command_buffer[current_frame]);
    assert(res == VK_SUCCESS);
}

void CreateSyncObjects(VulkanContext* context, MemoryArena* arena) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    context->image_acquire_semaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->renderFinishedSemaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->in_flight_fence = (VkFence*)arena_push(
        arena, sizeof(VkFence) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->image_acquire_semaphore[i]);
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->renderFinishedSemaphore[i]);
        vkCreateFence(context->device, &fenceInfo, nullptr,
                      &context->in_flight_fence[i]);
    }
}

void RecreateSwapchainResources(VulkanContext* context, MemoryArena* arena) {
    std::cerr << "Recreating swapchain\n";

    vkDeviceWaitIdle(context->device);

    for (int i = 0; i < context->swapchain_image_count; i++) {
        vkDestroyImageView(context->device, context->swapchain_image_views[i],
                           0);
    }

    context->old_swapchain = context->swapchain;
    CreateSwapchain(context, arena);
}

void RendererInit(VulkanContext* context, GLFWwindow* window,
                  MemoryArena* renderer_arena) {
    std::array<const char*, 1> validation_layers = {
        "VK_LAYER_KHRONOS_validation",
    };

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };

    uint32_t instance_api_version = 0;
    vkEnumerateInstanceVersion(&instance_api_version);

    assert(instance_api_version >= VK_API_VERSION_1_2);

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = instance_api_version;

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

    instance_info.enabledLayerCount = validation_layers.size();
    instance_info.ppEnabledLayerNames = validation_layers.data();
    instance_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

#else
    instance_info.enabledLayerCount = 0;
    instance_info.pNext = 0;
#endif

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> instance_extensions;
    instance_extensions.reserve(glfwExtensionCount + 10);
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        instance_extensions.emplace_back(glfwExtensions[i]);
    }

    instance_extensions.emplace_back(
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

#ifndef NDEBUG
    instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
    instance_extensions.emplace_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    instance_info.enabledExtensionCount = instance_extensions.size();
    instance_info.ppEnabledExtensionNames = instance_extensions.data();

    VkResult res = vkCreateInstance(&instance_info, 0, &context->instance);
    assert(res == VK_SUCCESS);

    auto vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkCreateDebugUtilsMessengerEXT");
    assert(vkCreateDebugUtilsMessengerEXT);
    vkCreateDebugUtilsMessengerEXT(context->instance, &debugCreateInfo, nullptr,
                                   &context->debug_messenger);

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &device_count, 0);
    VkPhysicalDevice* devices = (VkPhysicalDevice*)arena_push(
        renderer_arena, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(context->instance, &device_count, devices);
    context->physical_device =
        ChooseDiscreteGPU(context, devices, device_count);

    temp_arena tmparen = begin_temp_arena(renderer_arena);

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(context->physical_device, 0,
                                         &extensionCount, 0);
    VkExtensionProperties* available_device_extensions =
        (VkExtensionProperties*)arena_push(
            tmparen.parent, sizeof(VkExtensionProperties) * extensionCount);

    vkEnumerateDeviceExtensionProperties(context->physical_device, 0,
                                         &extensionCount,
                                         available_device_extensions);

    uint32_t non_equal = device_extensions.size();
    for (uint32_t i = 0; i < extensionCount; i++) {
        for (const char* ext : device_extensions) {
            if (strcmp(available_device_extensions[i].extensionName, ext) ==
                0) {
                std::cerr << "Found extension: "
                          << available_device_extensions[i].extensionName
                          << '\n';
                non_equal--;
            }
        }
    }

    if (non_equal > 0) {
        std::cerr << "Not all required device extensions are supported!\n";
        InvalidCodePath;
    }

    end_temp_arena(&tmparen);

    glfwCreateWindowSurface(context->instance, window, nullptr,
                            &context->surface);

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

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state_features{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = 0,
    };

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2_features{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext = &extended_dynamic_state_features,
    };


    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &sync2_features,
    };

    VkPhysicalDeviceFeatures2 physical_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &dynamic_rendering,
    };

    vkGetPhysicalDeviceFeatures2(context->physical_device, &physical_features2);

    if (dynamic_rendering.dynamicRendering == VK_FALSE) {
        std::cerr << "Dynamic rendering is not supported by the GPU!\n";
    }

    if (physical_features2.features.samplerAnisotropy == VK_FALSE) {
        std::cerr << "Sampler anisotropy is not supported by the GPU!\n";
    }
    if (physical_features2.features.sampleRateShading == VK_FALSE) {
        std::cerr << "Sample rate shading is not supported by the GPU!\n";
    }
    if (extended_dynamic_state_features.extendedDynamicState == VK_FALSE) {
        std::cerr << "Extended dynamic state is not supported by the GPU!\n";
    }

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT
        enabled_extended_dynamic_state_features{
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
            .pNext = 0,
            .extendedDynamicState = VK_TRUE,
        };

    VkPhysicalDeviceSynchronization2FeaturesKHR enable_sync2_features{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext = &enabled_extended_dynamic_state_features,
        .synchronization2 = VK_TRUE,
    };


    VkPhysicalDeviceDynamicRenderingFeatures enable_dynamic_rendering{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &enable_sync2_features,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 enable_physical_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enable_dynamic_rendering,
        .features =
            {
                .sampleRateShading = VK_TRUE,
                .samplerAnisotropy = VK_TRUE,
            },
    };

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enable_physical_features2,
        .queueCreateInfoCount = number_of_unique_queues,
        .pQueueCreateInfos = queueCreateInfos,
    };

#ifndef NDEBUG
    device_info.enabledLayerCount = validation_layers.size();
    device_info.ppEnabledLayerNames = validation_layers.data();
#else
    device_info.enabledLayerCount = 0;
#endif

    device_info.enabledExtensionCount = device_extensions.size();
    device_info.ppEnabledExtensionNames = device_extensions.data();

    res = vkCreateDevice(context->physical_device, &device_info, 0,
                         &context->device);
    assert(res == VK_SUCCESS);

    context->func_table.vkCmdBeginRenderingKHR =
        reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
            vkGetInstanceProcAddr(context->instance, "vkCmdBeginRenderingKHR"));
    context->func_table.vkCmdEndRenderingKHR =
        reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
            vkGetInstanceProcAddr(context->instance, "vkCmdEndRenderingKHR"));
    context->func_table.vkCmdPipelineBarrier2KHR =
        reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(vkGetInstanceProcAddr(
            context->instance, "vkCmdPipelineBarrier2KHR"));

    vkGetDeviceQueue(context->device, *q_indices.graphics, 0,
                     &context->graphics_queue);
    vkGetDeviceQueue(context->device, *q_indices.present, 0,
                     &context->present_queue);

    assert(context->graphics_queue != VK_NULL_HANDLE);
    assert(context->present_queue != VK_NULL_HANDLE);

    end_temp_arena(&tmp);

    CreateSwapchain(context, renderer_arena);

    CreateDescriptorSetLayout(context, renderer_arena);

    CreateCommandPool(context, renderer_arena);
    CreateDescriptorPool(context);

    CreateGraphicsPipeline(context, renderer_arena);
    CreateCommandBuffers(context, renderer_arena);

    CreateVertexBuffer(context);
    CreateIndexBuffer(context);
    CreateInstanceBuffer(context);
    CreateUniformBuffers(context, renderer_arena);

    CreateDescriptorSets(context, renderer_arena);

    CreateSyncObjects(context, renderer_arena);
}

void UpdateUniformBuffer(VulkanContext* context, uint32_t frame_index) {
    UniformBufferObject ubo = {};

    ubo.model = identity();
    ubo.view = identity();

    float aspect = context->swapchain_extent.width /
                   (float)context->swapchain_extent.height;

    ubo.proj = createOrthographicProjection(
        0.0f, static_cast<float>(context->swapchain_extent.width),
        static_cast<float>(context->swapchain_extent.height), 0.0f, -1.0f,
        1.0f);

    memcpy(context->uniform_buffers_mapped[frame_index], &ubo, sizeof(ubo));
}

void RendererDrawFrame(VulkanContext* context, MemoryArena* arena) {
    static uint32_t current_frame = 0;

    vkWaitForFences(context->device, 1,
                    &context->in_flight_fence[current_frame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(context->device, 1, &context->in_flight_fence[current_frame]);

    uint32_t swapchain_image_index;
    VkResult image_result =
        vkAcquireNextImageKHR(context->device, context->swapchain, UINT64_MAX,
                              context->image_acquire_semaphore[current_frame],
                              VK_NULL_HANDLE, &swapchain_image_index);

    std::cerr << "Swapchain: Image index: " << swapchain_image_index << '\n';

    if (image_result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchainResources(context, arena);
    }

    UpdateUniformBuffer(context, current_frame);

    vkResetCommandPool(context->device, context->command_pool, 0);
    RecordCommandBuffer(context, swapchain_image_index, arena, current_frame);

    VkSemaphore waitSemaphores[] = {
        context->image_acquire_semaphore[current_frame],
    };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphore signalSemaphores[] = {
        context->renderFinishedSemaphore[current_frame],
    };
    VkCommandBuffer commandBuffers[] = {
        context->command_buffer[current_frame],
    };

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .waitSemaphoreCount = ArrayCount(waitSemaphores),
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,

        .commandBufferCount = ArrayCount(commandBuffers),
        .pCommandBuffers = commandBuffers,

        .signalSemaphoreCount = ArrayCount(signalSemaphores),
        .pSignalSemaphores = signalSemaphores,
    };

    VkResult res = vkQueueSubmit(context->graphics_queue, 1, &submitInfo,
                                 context->in_flight_fence[current_frame]);

    if (res != VK_SUCCESS) {
        std::cerr << "Failed to submit draw command buffer: " << res << '\n';
    }

    VkSwapchainKHR swapChains[] = {context->swapchain};

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,

        .swapchainCount = 1,
        .pSwapchains = swapChains,

        .pImageIndices = &swapchain_image_index,
    };

    VkResult present_result =
        vkQueuePresentKHR(context->present_queue, &presentInfo);

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchainResources(context, arena);
    } else if (present_result != VK_SUCCESS) {
        InvalidCodePath;
    }

    current_frame = (current_frame + 1) % context->MAX_FRAMES_IN_FLIGHT;
}
