#include "vkh_renderer.h"
#include "vkh_memory.h"
#include "vkh_renderer_abstraction.h"

#include "vkh_math.cpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <sys/stat.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#define ArrayCount(x) (sizeof(x) / sizeof((x)[0]))

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
    fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);

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
            fprintf(stderr, "GPU found: GPU name: %s \n",
                    device_properties.deviceName);

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
            fprintf(stderr, "File size is not a multiple of 4\n");
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

#if SDL_PLATFORM_WINDOWS
    const char* vert_shader_path = "..\\shaders\\heart.vert.spv";
    const char* frag_shader_path = "..\\shaders\\heart.frag.spv";
#else
    const char* vert_shader_path = "./shaders/heart.vert.spv";
    const char* frag_shader_path = "./shaders/heart.frag.spv";
#endif

    my_file vert_shader_mf = readfile(vert_shader_path, arena);
    my_file frag_shader_mf = readfile(frag_shader_path, arena);

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

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]),
        .pDynamicStates = dynamicStates,
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDescriptions[2] = {};

    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex2D);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[6] = {};

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

    vertexInputInfo.vertexBindingDescriptionCount = sizeof(bindingDescriptions) / sizeof(bindingDescriptions[0]);
    vertexInputInfo.vertexAttributeDescriptionCount =
        sizeof(attributeDescriptions) / sizeof(attributeDescriptions[0]);
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

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
    // VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkExtent2D extent;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface, &capabilities);

    fprintf(stderr,
            "Surface capabilities: minImageCount = %d, maxImageCount = %d\n",
            capabilities.minImageCount, capabilities.maxImageCount);
    fprintf(stderr, "Current extent: width = %d, height = %d\n",
            capabilities.currentExtent.width, capabilities.currentExtent.height);


    uint32_t imageCount = capabilities.minImageCount;
    // assert(imageCount == 2);  // Double buffering by default

    // if (capabilities.maxImageCount > 0 &&
    //     imageCount > capabilities.maxImageCount) {
    //     imageCount = capabilities.maxImageCount;
    // }

    fprintf(stderr, "Engine Image Count: %d\n", imageCount);

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
            }
        }
        for (uint32_t i = 0; i < formatCount; ++i) {
            if (formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                formats[i].colorSpace ==
                    VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT) {
                // fprintf(stderr,"Found suitable format: "
                //           << string_VkFormat(formats[i].format)
                //           << " and colorspace: "
                //           << string_VkColorSpaceKHR(formats[i].colorSpace)
                //           << '\n';
                surfaceFormat = formats[i];
                break;
            }
        }

        if (surfaceFormat.format == VK_FORMAT_UNDEFINED) {
            surfaceFormat.format = VK_FORMAT_B8G8R8A8_SRGB;
            surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            fprintf(
                stderr,
                "No suitable format found, using default: %s, colorspace %s\n",
                string_VkFormat(surfaceFormat.format),
                string_VkColorSpaceKHR(surfaceFormat.colorSpace));
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
                fprintf(stderr, "Found mailbox present mode\n");
                presentMode = presentModes[i];
                break;
            }
            if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                fprintf(stderr, "Found immediate present mode\n");
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

    if (capabilities.currentExtent.height == UINT32_MAX || capabilities.currentExtent.width == -1 || capabilities.currentExtent.height == -1) {
        extent.width = context->WindowDrawableAreaWidth * context->WindowPixelDensity;
        extent.height = context->WindowDrawableAreaHeight * context->WindowPixelDensity;
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

    context->swapchain_image_count = imageCount;

    if (context->old_swapchain) {
    } else {
        context->swapchain_image_views = (VkImageView*)arena_push(
            parent_arena, imageCount * sizeof(VkImageView));
        context->swapchain_images =
            (VkImage*)arena_push(parent_arena, imageCount * sizeof(VkImage));
    }

    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            context->swapchain_images);

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

    fprintf(stderr, "Swapchain extent: %d, %d\n", context->swapchain_extent.width,
            context->swapchain_extent.height);
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

    return -1;
}

void CopyBuffer(VulkanContext* context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                VkDeviceSize size, VkDeviceSize dstOffset) {
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
    copyRegion.dstOffset = dstOffset;
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

void CreateDeviceStagingBuffer(VulkanContext* context,
                               MemoryArena* renderer_arena) {
    CreateBuffer(context, context->STAGING_BUFFER_SIZE,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 context->staging_buffer, context->staging_buffer_memory);

    context->staging_buffer_mapped =
        arena_push(renderer_arena, context->STAGING_BUFFER_SIZE);

    vkMapMemory(context->device, context->staging_buffer_memory, 0,
                context->STAGING_BUFFER_SIZE, 0,
                &context->staging_buffer_mapped);
}

void CreateDeviceMemoryBuffer(VulkanContext* context) {
    VkDeviceSize bufferSize = context->MAX_DEVICE_MEMORY_ALLOCATION_SIZE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(context->device, &bufferInfo, nullptr,
                   &context->device_memory_buffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(
        context->device, context->device_memory_buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(context->physical_device, memRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(context->device, &allocInfo, nullptr,
                     &context->device_memory_buffer_memory);

    vkBindBufferMemory(context->device, context->device_memory_buffer,
                       context->device_memory_buffer_memory, 0);
}

void CreateVertexBuffer(VulkanContext* context) {
    const std::vector<Vertex2D> vertices = {
        {1.0f, 0.0f},
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
    };

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    assert(bufferSize <= context->STAGING_BUFFER_SIZE);
    assert(context->vertex_buffer_size + bufferSize <=
           context->MAX_VERTEX_BUFFER_SIZE);
    context->vertex_buffer_size += bufferSize;

    memcpy(context->staging_buffer_mapped, vertices.data(), (size_t)bufferSize);

    CopyBuffer(context, context->staging_buffer, context->device_memory_buffer,
               bufferSize, context->vertex_buffer_offset);
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
                multiply(translate(x, y, 0.0f), scale(40.0f, 40.0f, 1.0f));

            instance.color = {
                (float)i / 5.0f,
                (float)j / 5.0f,
                0.5f,
            };

            instances.emplace_back(instance);
        }
    }

    VkDeviceSize bufferSize = sizeof(instances[0]) * instances.size();

    assert(bufferSize <= context->STAGING_BUFFER_SIZE);
    assert(context->instance_buffer_size + bufferSize <=
           context->MAX_INSTANCE_BUFFER_SIZE);

    context->instance_buffer_size += bufferSize;

    memcpy(context->staging_buffer_mapped, instances.data(),
           (size_t)bufferSize);

    CopyBuffer(context, context->staging_buffer, context->device_memory_buffer,
               bufferSize, context->instance_buffer_offset);
}

void CreateIndexBuffer(VulkanContext* context) {
    const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    assert(bufferSize <= context->STAGING_BUFFER_SIZE);
    assert(context->index_buffer_size + bufferSize <=
           context->MAX_INDEX_BUFFER_SIZE);

    context->index_buffer_size += bufferSize;

    memcpy(context->staging_buffer_mapped, indices.data(), (size_t)bufferSize);

    CopyBuffer(context, context->staging_buffer, context->device_memory_buffer,
               bufferSize, context->index_buffer_offset);
}

void CreateCommandBuffers(VulkanContext* context, MemoryArena* arena) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = context->MAX_FRAMES_IN_FLIGHT;

    context->command_buffers = (VkCommandBuffer*)arena_push(
        arena, sizeof(VkCommandBuffer) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult res = vkAllocateCommandBuffers(context->device, &allocInfo,
                                                &context->command_buffers[i]);
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
                         MemoryArena* arena, uint32_t current_frame,
                         PushBuffer* pb) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult res = vkBeginCommandBuffer(context->command_buffers[current_frame],
                                        &beginInfo);
    assert(res == VK_SUCCESS);

    TransitionImageLayout(context, context->command_buffers[current_frame],
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
        context->command_buffers[current_frame], &renderingInfo);

    vkCmdBindPipeline(context->command_buffers[current_frame],
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      context->graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)context->swapchain_extent.width;
    viewport.height = (float)context->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context->command_buffers[current_frame], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context->swapchain_extent;
    vkCmdSetScissor(context->command_buffers[current_frame], 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {context->device_memory_buffer,
                                context->device_memory_buffer};
    VkDeviceSize offsets[] = {context->vertex_buffer_offset,
                              context->instance_buffer_offset};

    vkCmdBindVertexBuffers(context->command_buffers[current_frame], 0, 2,
                           vertexBuffers, offsets);
    vkCmdBindIndexBuffer(context->command_buffers[current_frame],
                         context->device_memory_buffer,
                         context->index_buffer_offset, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        context->command_buffers[current_frame],
        VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, 1,
        &context->descriptor_sets[current_frame], 0, nullptr);

    vkCmdDrawIndexed(context->command_buffers[current_frame], 6,
                     pb->number_of_entries, 0, 0, 0);

    context->func_table.vkCmdEndRenderingKHR(
        context->command_buffers[current_frame]);

    TransitionImageLayout(context, context->command_buffers[current_frame],
                          context->swapchain_images[image_index],
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);

    res = vkEndCommandBuffer(context->command_buffers[current_frame]);
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
    context->render_finished_semaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->in_flight_fence = (VkFence*)arena_push(
        arena, sizeof(VkFence) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->image_acquire_semaphore[i]);
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->render_finished_semaphore[i]);
        vkCreateFence(context->device, &fenceInfo, nullptr,
                      &context->in_flight_fence[i]);
    }
}

void RecreateSwapchainResources(VulkanContext* context, MemoryArena* arena) {
    fprintf(stderr, "Recreating swapchain\n");

    vkDeviceWaitIdle(context->device);

    for (int i = 0; i < context->swapchain_image_count; i++) {
        vkDestroyImageView(context->device, context->swapchain_image_views[i],
                           0);
    }
    context->old_swapchain = context->swapchain;
    CreateSwapchain(context, arena);

    // Recreate sync objects (reuse memory, note that it will not work if
    // MAX_FRAMES_IN_FLIGHT has been changed - needs more or less memory)
    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(context->device, context->in_flight_fence[i], 0);
        vkDestroySemaphore(context->device, context->image_acquire_semaphore[i],
                           0);
        vkDestroySemaphore(context->device,
                           context->render_finished_semaphore[i], 0);
        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->image_acquire_semaphore[i]);
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->render_finished_semaphore[i]);
        vkCreateFence(context->device, &fenceInfo, nullptr,
                      &context->in_flight_fence[i]);
    }
}

void RendererInit(VulkanContext* context, SDL_Window* window,
                  MemoryArena* renderer_arena) {
    const char* validation_layers[] = {
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

    std::vector<const char*> instance_extensions;

    uint32_t instance_api_version = 0;
    vkEnumerateInstanceVersion(&instance_api_version);

    assert(instance_api_version >= VK_API_VERSION_1_3);

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

#ifdef VKH_DEBUG
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

    instance_info.enabledLayerCount = sizeof(validation_layers) / sizeof(const char*);
    instance_info.ppEnabledLayerNames = validation_layers;
    instance_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

#else
    instance_info.enabledLayerCount = 0;
    instance_info.pNext = 0;
#endif

    uint32_t glfwExtensionCount = 0;
    char const* const* glfwExtensions;
    glfwExtensions = SDL_Vulkan_GetInstanceExtensions(&glfwExtensionCount);

    instance_extensions.reserve(glfwExtensionCount + 10);
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        instance_extensions.emplace_back(glfwExtensions[i]);
    }

    instance_extensions.emplace_back(
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    instance_extensions.emplace_back(
        VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    instance_extensions.emplace_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifdef VKH_DEBUG
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
#ifdef VKH_DEBUG
    auto vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkCreateDebugUtilsMessengerEXT");
    assert(vkCreateDebugUtilsMessengerEXT);
    vkCreateDebugUtilsMessengerEXT(context->instance, &debugCreateInfo, nullptr,
                                   &context->debug_messenger);
#endif

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &device_count, 0);
    VkPhysicalDevice* devices = (VkPhysicalDevice*)arena_push(
        renderer_arena, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(context->instance, &device_count, devices);
    context->physical_device =
        ChooseDiscreteGPU(context, devices, device_count);

    context->physical_device_properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = 0,
    };

    vkGetPhysicalDeviceProperties2(context->physical_device,
                                   &context->physical_device_properties2);

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
                fprintf(stderr, "Found extension: %s\n",
                        available_device_extensions[i].extensionName);
                non_equal--;
            }
        }
    }

    if (non_equal > 0) {
        fprintf(stderr, "Not all required device extensions are supported!\n");
    }

    end_temp_arena(&tmparen);

    SDL_Vulkan_CreateSurface(window, context->instance, 0, &context->surface);

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

    VkPhysicalDeviceVulkan13Features vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = 0,

    };

    VkPhysicalDeviceFeatures2 physical_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13_features,
    };

    vkGetPhysicalDeviceFeatures2(context->physical_device, &physical_features2);

    if (vk13_features.dynamicRendering == VK_FALSE) {
        fprintf(stderr, "Dynamic rendering is not supported by the GPU!\n");
    }
    if (vk13_features.synchronization2 == VK_FALSE) {
        fprintf(stderr, "Synchronization 2 is not supported by the GPU!\n");
    }

    if (physical_features2.features.samplerAnisotropy == VK_FALSE) {
        fprintf(stderr, "Sampler anisotropy is not supported by the GPU!\n");
    }
    if (physical_features2.features.sampleRateShading == VK_FALSE) {
        fprintf(stderr, "Sample rate shading is not supported by the GPU!\n");
    }

    VkPhysicalDeviceVulkan13Features enable_vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = 0,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 enable_physical_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enable_vk13_features,
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

#ifdef VKH_DEBUG
    device_info.enabledLayerCount = sizeof(validation_layers) / sizeof(const char*);
    device_info.ppEnabledLayerNames = validation_layers;
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
    CreateSyncObjects(context, renderer_arena);

    CreateDescriptorSetLayout(context, renderer_arena);

    CreateCommandPool(context, renderer_arena);
    CreateDescriptorPool(context);

    CreateGraphicsPipeline(context, renderer_arena);
    CreateCommandBuffers(context, renderer_arena);

    CreateDeviceMemoryBuffer(context);
    CreateDeviceStagingBuffer(context, renderer_arena);

    // TODO: Allocate from host visible memory
    CreateUniformBuffers(context, renderer_arena);

    CreateDescriptorSets(context, renderer_arena);
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

// NOTE: THIS WILL NOT WORK PROPERLY BECAUSE ITS NOT SORTED OR PROCESSED
// WHATSOEVER
void UploadPushBufferContentsToGPU(VulkanContext* context, PushBuffer* pb,
                                   MemoryArena* arena) {
    temp_arena tmp = begin_temp_arena(arena);

    uint32_t number_of_entries = pb->number_of_entries;

    InstanceData* all_instances = (InstanceData*)arena_push(
        tmp.parent, sizeof(InstanceData) * number_of_entries);

    for (size_t i = 0; i < number_of_entries; i++) {
        PushBufferEntry* pbe =
            (PushBufferEntry*)(pb->arena.base + i * sizeof(PushBufferEntry));

        if (pbe->type == QUAD) {
            if (i == 0) {
                // First entry, upload vertices
                CreateVertexBuffer(context);
                CreateIndexBuffer(context);
            }

            InstanceData instance;

            float x = pbe->data.quad.x;
            float y = pbe->data.quad.y;

            instance.transform = multiply(
                scale(pbe->data.quad.width, pbe->data.quad.height, 1.0f),
                translate(x, y, 0.0f));
            instance.color = {
                pbe->color[0],
                pbe->color[1],
                pbe->color[2],
            };

            all_instances[i] = instance;
        }
    }

    if (number_of_entries > 0) {
        VkDeviceSize all_instances_size =
            sizeof(InstanceData) * number_of_entries;
        assert(all_instances_size <= context->MAX_INSTANCE_BUFFER_SIZE);
        context->instance_buffer_size += all_instances_size;
        memcpy(context->staging_buffer_mapped, all_instances,
               all_instances_size);
        CopyBuffer(context, context->staging_buffer,
                   context->device_memory_buffer, all_instances_size,
                   context->instance_buffer_offset);
    }
    end_temp_arena(&tmp);
}

void RendererDrawFrame(VulkanContext* context, MemoryArena* arena,
                       PushBuffer* push_buffer) {
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

    if (image_result == VK_ERROR_OUT_OF_DATE_KHR ||
        image_result == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchainResources(context, arena);
        return;
    }

    UpdateUniformBuffer(context, current_frame);

    // Update Vertex and Index buffers if needed
    UploadPushBufferContentsToGPU(context, push_buffer, arena);

    vkResetCommandBuffer(context->command_buffers[current_frame], 0);
    RecordCommandBuffer(context, swapchain_image_index, arena, current_frame,
                        push_buffer);

    VkSemaphore waitSemaphores[] = {
        context->image_acquire_semaphore[current_frame],
    };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphore signalSemaphores[] = {
        context->render_finished_semaphore[current_frame],
    };
    VkCommandBuffer commandBuffers[] = {
        context->command_buffers[current_frame],
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
        fprintf(stderr, "Failed to submit draw command buffer: %s",
                string_VkResult(res));
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
    }

    current_frame = (current_frame + 1) % context->MAX_FRAMES_IN_FLIGHT;
}
