// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"

// 2. LIB
#include <vulkan/vulkan.h>

// 3. WIN
#include <vector>
#include <stdexcept>
#include <string>
#include <fstream>
using namespace std;


vector<char> readFile(const string &filename);
VkShaderModule createShaderModule(shared_ptr<VulkanDevice> device, const vector<char> &code);

class VulkanPipeline
{
    public:
        VkPipelineLayout layout;
        VkPipeline pipeline;

        #pragma region initializers
        static VkPipelineShaderStageCreateInfo initVertexShaderStageCI(
            VkShaderModule module);

        static VkPipelineShaderStageCreateInfo initFragmentShaderStageCI(
            VkShaderModule module);

        static VkPipelineShaderStageCreateInfo initComputeShaderStageCI(
            VkShaderModule module);

        static VkPipelineVertexInputStateCreateInfo initVertexStageInputStateCI(
            const vector<VkVertexInputBindingDescription> &bindingDescriptions,
            const vector<VkVertexInputAttributeDescription> &attributeDescriptions);

        static VkPipelineInputAssemblyStateCreateInfo initInputAssemblyStateCI(
            VkPrimitiveTopology topology, 
            VkBool32 primitiveRestartEnable);

        static VkViewport initViewport(
            float x, float y,
            float width, float height,
            float mindepth, float maxdepth);

        static VkPipelineViewportStateCreateInfo initViewportStateCI(
            bool dynamicState,
            const VkViewport &viewport,
            const VkRect2D &scissor);

        static VkPipelineViewportStateCreateInfo initViewportStateCI(
            bool dynamicState,
            uint32_t viewportCount,
            uint32_t scissorCount,
            const vector<VkViewport> &viewports,
            const vector<VkRect2D> &scissors);

        static VkPipelineRasterizationStateCreateInfo initRaserizationStateCI(
            VkPolygonMode polygonMode,
            VkCullModeFlags cullMode,
            VkFrontFace frontFace,
            VkBool32 depthClampEnable = VK_FALSE,
            VkBool32 rasterizerDiscardEnable = VK_FALSE,
            float lineWidth = 1.0f);

        static VkPipelineMultisampleStateCreateInfo initMultisampleStateCI(
            VkBool32 enableSampleShading,
            float minSampleShading,
            VkSampleCountFlagBits rasterizationSamples);

        static VkPipelineDepthStencilStateCreateInfo initDepthStencilStateCI(
            VkBool32 depthTestEnable,
            VkBool32 depthWriteEnable,
            VkCompareOp depthCompareOp,
            VkBool32 stencilTestEnable);

        static VkPipelineColorBlendAttachmentState initColorBlendAttachment(
            VkColorComponentFlags colorWriteMask,
            VkBool32 blendEnable);

        static VkPipelineColorBlendAttachmentState initColorBlendAttachmentAlpha0ignore(
            VkColorComponentFlags colorWriteMask,
            VkBool32 blendEnable);

        static VkPipelineColorBlendAttachmentState initColorBlendAttachmentSrcAlphaDst(
            VkColorComponentFlags colorWriteMask,
            VkBool32 blendEnable);

        static VkPipelineColorBlendStateCreateInfo initColorBlendStateCI(
            uint32_t attachmentCount,
            const vector<VkPipelineColorBlendAttachmentState> &pAttachments);

        static VkPipelineColorBlendStateCreateInfo initColorBlendStateCI(
            const VkPipelineColorBlendAttachmentState &pAttachment);

        static VkPipelineLayoutCreateInfo initPiplineLayoutCI(
            const VkDescriptorSetLayout &pSetLayout);

        static VkPipelineLayoutCreateInfo initPiplineLayoutCI(
            uint32_t setLayoutCount,
            const vector<VkDescriptorSetLayout> &pSetLayouts);

        #pragma endregion initializers

        // Graphics Pipeline
        VulkanPipeline(
            shared_ptr<VulkanDevice> device,
            uint32_t stageCount,
            const vector<VkPipelineShaderStageCreateInfo> pShaderStages,
            const VkPipelineVertexInputStateCreateInfo &vertexInputStage,
            const VkPipelineInputAssemblyStateCreateInfo &inputAssemblyState,
            const VkPipelineViewportStateCreateInfo &viewportState,
            const VkPipelineRasterizationStateCreateInfo &rasterizationState,
            const VkPipelineMultisampleStateCreateInfo &multisampleState,
            const VkPipelineDepthStencilStateCreateInfo &depthStencilState,
            const VkPipelineColorBlendStateCreateInfo &colorBlendState,
            const VkPipelineLayoutCreateInfo &layoutCreateInfo,
            const VkRenderPass renderPass,
            const uint32_t subpass);

        // ComputePipeline
        VulkanPipeline(
            shared_ptr<VulkanDevice> device,
            const VkPipelineLayoutCreateInfo &layoutCreateInfo,
            const VkPipelineShaderStageCreateInfo &shaderStage
        );

        ~VulkanPipeline();
        
    private:
        shared_ptr<VulkanDevice> device;
};