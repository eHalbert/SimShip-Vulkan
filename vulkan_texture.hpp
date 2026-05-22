/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"
#include "vulkan_buffer.hpp"
#include "Utility.h"

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

class VulkanTexture
{
public:
    VkImage             image           = nullptr;  // logical descriptor (format, extent, usage, layout...)
    VkImageView         imageView       = nullptr;
    VkSampler           sampler         = nullptr;
    VkDeviceMemory      gpuMemory       = nullptr;  // physical memory block on the GPU
    VkDeviceMemory      cpuMemory       = nullptr;  // physical memory block on the CPU
	void              * cpuData         = nullptr;  // CPU pointer to stagingMemory (if persistent staging)
    VkExtent3D          extent          = {};
    VkFormat            format          = VK_FORMAT_UNDEFINED;
    uint32_t            layerCount      = 1;
    uint32_t            mipLevels       = 1;

	bool                bTransparency   = false;  

    VulkanTexture() = default;
    VulkanTexture(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageAspectFlags aspectFlags);
    ~VulkanTexture();
    void Create(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, bool persistentStaging);
    void CreateFromData(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, bool persistentStaging, const float* data);
    bool CreateFromFile(shared_ptr<VulkanDevice> device, const string& filename, bool bMipMap = false);
    void CreateDummyTexture(shared_ptr<VulkanDevice> device, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
    void CopyStagingToGPU();
    void Save(const string& fullname);
    void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
    void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer cmd);
    void GenerateMipmaps();
    void CreateImGuiDescriptor(VkDescriptorPool pool, VkDescriptorSetLayout layout);
    VkDescriptorSet GetImGuiDescriptorSet() const { return mImguiDescriptorSet; }
    
    void CreateTexture2DArray(shared_ptr<VulkanDevice> device, const string& basePath, const string& baseFilename, int texCount, int width, int height);


private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    VkBuffer                    mStagingBuffer = VK_NULL_HANDLE;

    VkImageLayout               mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDescriptorSet             mImguiDescriptorSet = VK_NULL_HANDLE;

	bool						mbDestroyed = false;

    void CreateImageInternal(uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
        VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageAspectFlags aspectFlags);
    void CreateImageViewInternal(VkImageViewType viewType, uint32_t layerCount, uint32_t mipLevels, VkImageAspectFlags aspectFlags);

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory);
    void CopyBufferToImage(VkBuffer buffer, VkCommandBuffer cmd);
    void CreateSamplerRepeat();
    void CreateSamplerClampToEdge();
    uint32_t FormatSize(VkFormat format);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer SingleTimeCommands(std::function<void(VkCommandBuffer)> func);

};