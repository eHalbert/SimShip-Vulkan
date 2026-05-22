/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"
#include "vulkan_texture.hpp"
#include "Utility.h"
#include "Structures.h"

// 2. LIB
#include <vulkan/vulkan.h>
#include <stb/stb_image.h>
#include "stb/stb_image_write.h"
#include "tinyexr/tinyexr.h"
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <iostream>
using namespace std;

class Overlay
{
public:
    Overlay(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
    void CreateOverlayPipeline();
    void RenderOverlay(VkCommandBuffer cmd, const VulkanTexture& texture, vec2 topLeft, vec2 size);

private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    VkRenderPass                mRenderPass;
    VkExtent2D                  mExtent;

    sPipeline_1                  mOverlayPipeline;

    void CreateOverlayDescriptors();
    void UpdateOverlayDescriptors(const VulkanTexture& texture);

};

