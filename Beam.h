/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"
#include "vulkan_device.hpp"
#include "vulkan_ubo.hpp"
#include "Camera.h"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#include <array>
#include <memory>
using namespace std;


class Beam
{
public:
    Beam() {};
	~Beam();
    void Init(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, float range);

    void Render(VkCommandBuffer commandBuffer, uint32_t frame, const mat4& model, const mat4& view, const mat4& proj, vec3 color, float intensity);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

    bool                        bVisible = true;

private:
    void CreateGeometry(float range);
    void CreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    shared_ptr<VulkanDevice>    mVulkanDevice;
    uint32_t                    mFramesInFlight = 2;

    sPipeline_x                  mPipeline;
    unique_ptr<VulkanUBO>       mVertexBuffer;
    unique_ptr<VulkanUBO>       mIndiceBuffer;
    uint32_t                    mIndiceCount;
    
    struct sLightPushConstants
    {
        mat4  model;
        mat4  view;
        mat4  proj;

        vec3  color;
        float intensity;
    };

    struct sVertexBeam
    {
        vec3    pos;
        vec3    normal;
        float   t;

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(sVertexBeam);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
        {
            array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            // Location 0: Position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(sVertexBeam, pos);

            // Location 1: Normal
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(sVertexBeam, normal);

            // Location 2: TexCoord
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(sVertexBeam, t);

            return attributeDescriptions;
        }
    };
};

