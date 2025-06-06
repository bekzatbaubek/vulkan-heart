#include <assert.h>
#include <sys/stat.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

// #define GLFW_INCLUDE_VULKAN
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include "GLFW/glfw3.h"
#include "vkh_game.h"
#include "vkh_memory.cpp"
#include "vkh_renderer.h"
#include "vulkan/vulkan_core.h"

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

            context->device_features = device_features;
            context->device_properties = device_properties;

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
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

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

    // Instance transform matrix - 4 vec4 rows (locations 1-4)
    for (int i = 0; i < 4; i++) {
        attributeDescriptions[1 + i].binding = 1;
        attributeDescriptions[1 + i].location = 1 + i;
        attributeDescriptions[1 + i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1 + i].offset =
            i * sizeof(float) * 4;  // 16 bytes per row
    }

    // Instance color (location 5)
    attributeDescriptions[5].binding = 1;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[5].offset = sizeof(float) * 16;  // After the matrix

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
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context->descriptor_set_layout;

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

void CreateSwapchain(VulkanContext* context, MemoryArena* parent_arena) {
    // Check whether the device meets requirements
    VkSurfaceFormatKHR surfaceFormat;
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

    // uint32_t imageCount = context->MAX_FRAMES_IN_FLIGHT;

    std::cerr << "Engine Image Count" << imageCount << '\n';

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
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                std::cerr << "Found suitable format"
                          << string_VkFormat(formats[i].format)
                          << " and colorspace: "
                          << string_VkColorSpaceKHR(formats[i].colorSpace)
                          << '\n';
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
                std::cerr << "Found mailbox present mode\n";
                presentMode = presentModes[i];
                break;
            }
            if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                std::cerr << "Found immediate present mode\n";
                // presentMode = presentModes[i];
                // break;
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

    VkImage* swapchain_images =
        (VkImage*)arena_push(parent_arena, imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &imageCount,
                            swapchain_images);

    context->swapchain_image_count = imageCount;

    if (context->old_swapchain) {
        // Recreating swapchain, no need to realloc memory
    } else {
        context->swapchain_images = (VkImageView*)arena_push(
            parent_arena, imageCount * sizeof(VkImageView));
    }

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

        VkResult res = vkCreateImageView(context->device, &viewInfo, nullptr,
                                         &context->swapchain_images[i]);
    }

    context->swapchain_format = surfaceFormat.format;
    context->swapchain_extent = extent;

    std::cerr << "Swapchain extent: " << context->swapchain_extent.width << ", "
              << context->swapchain_extent.height << '\n';
}

void CreateRenderPass(VulkanContext* context) {
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

void CreateFramebuffers(VulkanContext* context, MemoryArena* arena,
                        bool recreate) {
    if (recreate) {
    } else {
        context->framebuffers = (VkFramebuffer*)arena_push(
            arena, sizeof(VkFramebuffer) * context->swapchain_image_count);
    }

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
    copyRegion.srcOffset = 0;  // Optional
    copyRegion.dstOffset = 0;  // Optional
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
        descriptorWrite.pImageInfo = nullptr;        // Optional
        descriptorWrite.pTexelBufferView = nullptr;  // Optional

        vkUpdateDescriptorSets(context->device, 1, &descriptorWrite, 0,
                               nullptr);
    }
}

void CreateInstanceBuffer(VulkanContext* context) {
    std::vector<InstanceData> instances;
    instances.reserve(5 * 5);

    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            InstanceData instance;

            float x = i * 1.0f;
            float y = j * 1.0f;

            instance.transform =
                multiply(translate(x, y, 0.0f), scale(200.0f, 190.0f, 1.0f));

            instance.color = {(float)i / 5.0f, (float)j / 5.0f, 0.5f};

            instances.emplace_back(instance);
        }
    }

    instances[16].transform =
        multiply(scale(200.0f, 190.0f, 1.0f), translate(20.0f, 20.0f, -0.1f));

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

void RecordCommandBuffer(VulkanContext* context, uint32_t image_index,
                         MemoryArena* arena, uint32_t current_frame) {
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

    vkCmdEndRenderPass(context->command_buffer[current_frame]);

    res = vkEndCommandBuffer(context->command_buffer[current_frame]);
    assert(res == VK_SUCCESS);
}

void CreateSyncObjects(VulkanContext* context, MemoryArena* arena) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Fence is signalled first, so the drawFrame call can wait for it
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    context->image_available_semaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->renderFinishedSemaphore = (VkSemaphore*)arena_push(
        arena, sizeof(VkSemaphore) * context->MAX_FRAMES_IN_FLIGHT);
    context->in_flight_fence = (VkFence*)arena_push(
        arena, sizeof(VkFence) * context->MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < context->MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                          &context->image_available_semaphore[i]);
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
        vkDestroyImageView(context->device, context->swapchain_images[i], 0);
        vkDestroyFramebuffer(context->device, context->framebuffers[i], 0);
    }
    context->old_swapchain = context->swapchain;

    CreateSwapchain(context, arena);
    CreateFramebuffers(context, arena, true);

    vkDestroyRenderPass(context->device, context->render_pass, nullptr);

    CreateRenderPass(context);

    // vkDestroySwapchainKHR(context->device, context->old_swapchain, nullptr);
}

void RendererInit(VulkanContext* context, GLFWwindow* window,
                  MemoryArena* renderer_arena) {
    std::array<const char*, 1> validation_layers = {
        "VK_LAYER_KHRONOS_validation"};

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
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
    instance_extensions.reserve(glfwExtensionCount + 2);
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        instance_extensions.emplace_back(glfwExtensions[i]);
    }

#ifndef NDEBUG
    instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
    instance_extensions.emplace_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    instance_info.enabledExtensionCount = instance_extensions.size();
    instance_info.ppEnabledExtensionNames = instance_extensions.data();

#ifdef __APPLE__
    instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

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

    // 3. Logical Device

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

    vkGetDeviceQueue(context->device, *q_indices.graphics, 0,
                     &context->graphics_queue);
    vkGetDeviceQueue(context->device, *q_indices.present, 0,
                     &context->present_queue);

    assert(context->graphics_queue != VK_NULL_HANDLE);
    assert(context->present_queue != VK_NULL_HANDLE);

    end_temp_arena(&tmp);

    // 4. Swapchain
    CreateSwapchain(context, renderer_arena);

    CreateDescriptorSetLayout(context, renderer_arena);

    CreateCommandPool(context, renderer_arena);
    CreateDescriptorPool(context);

    CreateRenderPass(context);
    CreateGraphicsPipeline(context, renderer_arena);
    CreateFramebuffers(context, renderer_arena, false);
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

    uint32_t image_index;
    VkResult image_result =
        vkAcquireNextImageKHR(context->device, context->swapchain, UINT64_MAX,
                              context->image_available_semaphore[current_frame],
                              VK_NULL_HANDLE, &image_index);

    if (image_result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchainResources(context, arena);
        return;
    } else if (image_result != VK_SUCCESS &&
               image_result != VK_SUBOPTIMAL_KHR) {
        InvalidCodePath;
    }

    UpdateUniformBuffer(context, current_frame);

    vkResetCommandBuffer(context->command_buffer[current_frame], 0);
    RecordCommandBuffer(context, image_index, arena, current_frame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {
        context->image_available_semaphore[current_frame]};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context->command_buffer[current_frame];

    VkSemaphore signalSemaphores[] = {
        context->renderFinishedSemaphore[current_frame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult res = vkQueueSubmit(context->graphics_queue, 1, &submitInfo,
                                 context->in_flight_fence[current_frame]);

    if (res != VK_SUCCESS) {
        std::cerr << "Failed to submit draw command buffer: " << res << '\n';
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {context->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &image_index;

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
