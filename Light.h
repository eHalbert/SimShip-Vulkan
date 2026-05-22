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


class Light
{
public:
    Light(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
    ~Light();

    // Called once per frame for each navigation light
    void Render(VkCommandBuffer cmd, Camera& camera, vec3 lightPosition, vec3 lightColor, float lightIntensity = 1.0f, float starIntensity = 0.1f);
    void Render(VkCommandBuffer cmd, Camera& camera, mat4 model, vec3 lightColor, float lightIntensity, float starIntensity);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

private:

    struct sLightPushConstants
    {
        mat4  model;
        mat4  view;
        mat4  proj;
        
        vec3  lightColor;     
        float lightIntensity;
        
        float starIntensity;
        float pad[3];
    };
    struct sVertexLight
    {
        vec2 position;  // location 0
    };

    void CreateGeometry();
    void CreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    shared_ptr<VulkanDevice>    mVulkanDevice;

    sPipeline_x                 mPipeline;
    unique_ptr<VulkanUBO>       mVertexBuffer;
};
