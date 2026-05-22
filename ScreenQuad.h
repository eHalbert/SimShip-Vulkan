/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once
// 1. PROJET
#include "Utility.h"
#include "vulkan_device.hpp"
#include "vulkan_ubo.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#include <array>
#include <memory>
using namespace std;


class ScreenQuad
{
public:
    ScreenQuad(shared_ptr<VulkanDevice> vulkanDevice, unique_ptr<VulkanSwapChain>& swapChain)
    {
        mVulkanDevice = vulkanDevice;
        mFramesInFlight = swapChain->imageCount;

        // Données du quad (même vertices que OpenGL)
        float vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f
        };

        // Vertex buffer
        CreateVertexBuffer(vertices, sizeof(vertices));
    }
    ~ScreenQuad() 
    {
        vkDestroyBuffer(mVulkanDevice->device, mVertexBuffer, nullptr);
        vkFreeMemory(mVulkanDevice->device, mVertexBufferMemory, nullptr);
		mPipeline.destroy(mVulkanDevice->device);
    }

    static struct sPostProcessingUBO {
        float   exposure; 
        float   zNear;
        float   zFar;
        float   horizonHeight;
        vec3    eyePos;
        int	    bOcean;
        vec3    oceanColor;
        float   fogDensity;
        vec3    fogColor;
        float   uTime;        
        vec2    screenSize;   
        int     bLowIntensity;
        int     bNightVision;
        vec3    sunDirection;
        float   mieExponent;
        vec3    sunColor;
        int     bBinoculars;
		int	    bRainDropsTrails;
        int     bRainBlurDrips;
		int	    bInside;
    };

    void CreateGraphicsPipeline(VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples, VkExtent2D swapChainExtent)
    {
		// Previous cleaned up if any
        mPipeline.destroy(mVulkanDevice->device);

        // 1. Shaders
        auto vertCode = CompileShaderRuntime("Resources/Shaders/Misc/post_processing.vert");
        auto fragCode = CompileShaderRuntime("Resources/Shaders/Misc/post_processing.frag");
        VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
        VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
        };

        // 2. Vertex input
        auto bindingDescription = Vertex::getBindingDescription(); 
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = 2;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // 3. Descriptor Set Layout
        array<VkDescriptorSetLayoutBinding, 4> bindings = { {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // stencil
        } };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

        // 4. Pipeline Layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
        vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

        // 5. Input assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 6. Viewport & scissor
        VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, swapChainExtent };

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // 7. Rasterizer (no culling, no depth bias)
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // 8. Multisampling 1x
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;  
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;

        // 9. Depth stencil (disabled)
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // 10. Color blending (opaque)
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // 11. Dynamic state
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 0;
        dynamicState.pDynamicStates = nullptr;

        // 12. Pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = mPipeline.pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);
    
        vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
        vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

        CreateDescriptors();
    }
    void UpdateDescriptorSet(VkImageView imageViewColor, VkImageView imageViewDepth, VkImageView imageViewStencil, VkSampler samplerStencil)
    {
        for (size_t i = 0; i < mFramesInFlight; i++)
        {
            // UBO (binding 0)
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = mPipeline.ubo[i]->buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(sPostProcessingUBO);

            // Color MSAA (binding 1)
            VkDescriptorImageInfo imageInfoColor{};
            imageInfoColor.imageView = imageViewColor;  // g_ColorViewResolve(1x)
            imageInfoColor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfoColor.sampler = mSamplerColor;

            // Depth 1x (binding 2)
            VkDescriptorImageInfo imageInfoDepth{};
            imageInfoDepth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfoDepth.imageView = imageViewDepth;  // g_DepthViewResolve (1x)
            imageInfoDepth.sampler = mSamplerDepth;

            // Stencil (binding 3)
            VkDescriptorImageInfo imageInfoStencil{};
            imageInfoStencil.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfoStencil.imageView = imageViewStencil;
            imageInfoStencil.sampler = samplerStencil;

            VkWriteDescriptorSet descriptorWrites[4]{};

            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = mPipeline.descSet[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = mPipeline.descSet[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].pImageInfo = &imageInfoColor;

            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = mPipeline.descSet[i];
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[2].pImageInfo = &imageInfoDepth;

            descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; 
            descriptorWrites[3].dstSet = mPipeline.descSet[i];
            descriptorWrites[3].dstBinding = 3;
            descriptorWrites[3].descriptorCount = 1;
            descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[3].pImageInfo = &imageInfoStencil;

            vkUpdateDescriptorSets(mVulkanDevice->device, 4, descriptorWrites, 0, nullptr);
        }
    }
    void UpdateUbo(int iCurrentFrame, sPostProcessingUBO& uboData)
    {
        memcpy(mPipeline.ubo[iCurrentFrame]->data, &uboData, sizeof(sPostProcessingUBO));
	}
    void Render(VkCommandBuffer commandBuffer, int iCurrentFrame)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[iCurrentFrame], 0, nullptr);

        VkBuffer vertexBuffers[] = { mVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

private:
    void CreateVertexBuffer(float* vertices, size_t size) 
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(mVulkanDevice->device, &bufferInfo, nullptr, &mVertexBuffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(mVulkanDevice->device, mVertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(mVulkanDevice->physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &mVertexBufferMemory);
        vkBindBufferMemory(mVulkanDevice->device, mVertexBuffer, mVertexBufferMemory, 0);

        void* data;
        vkMapMemory(mVulkanDevice->device, mVertexBufferMemory, 0, size, 0, &data);
        memcpy(data, vertices, size);
        vkUnmapMemory(mVulkanDevice->device, mVertexBufferMemory);
    }
    void CreateDescriptors()
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mSamplerColor);
        vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mSamplerDepth);
        
        mPipeline.descSet.resize(mFramesInFlight);
        mPipeline.ubo.resize(mFramesInFlight); 
        
        for (size_t i = 0; i < mFramesInFlight; i++)
            mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(ScreenQuad::sPostProcessingUBO));
        
        VkDescriptorPoolSize poolSizes[2] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * mFramesInFlight},
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * mFramesInFlight}
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = mFramesInFlight;
        vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

        // Global sets (1 per frame)
        vector<VkDescriptorSetLayout> layouts(mFramesInFlight, mPipeline.descSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = mPipeline.descPool;
        allocInfo.descriptorSetCount = layouts.size();
        allocInfo.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());
    }

    shared_ptr<VulkanDevice> mVulkanDevice;
    uint32_t                mFramesInFlight;
    sPipeline_x              mPipeline;
    VkBuffer                mVertexBuffer;
    VkDeviceMemory          mVertexBufferMemory;
    VkSampler               mSamplerColor;
    VkSampler               mSamplerDepth;

    struct Vertex 
    {
        vec2 pos;
        vec2 texCoord;

        static VkVertexInputBindingDescription getBindingDescription() 
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = 4 * sizeof(float);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() 
        {
            array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[0].offset = 0;

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[1].offset = 2 * sizeof(float);

            return attributeDescriptions;
        }
    };
};
