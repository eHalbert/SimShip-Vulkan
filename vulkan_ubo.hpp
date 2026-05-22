/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"

// 2. LIB
#include <vulkan/vulkan.h>

// 3. WIN
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace std;


class VulkanUBO
{
public:
    VulkanUBO(shared_ptr<VulkanDevice>& vulkanDevice, VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	~VulkanUBO();
    void Flush();

	VkDeviceSize GetSize() const { return mSize; }

    shared_ptr<VulkanDevice>    mVulkanDevice;
    VkDeviceSize                mSize;

    VkBuffer            buffer = nullptr;
    VkDeviceMemory      memory = nullptr;
    void              * data = nullptr;         // Pointeur mapped
    bool                hasCoherentMemory = false;

private:
    void CreateBuffer(VkBufferUsageFlags usage);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void HasCoherentMemory();
    uint32_t GetMemoryTypeIndex();
    VkDeviceSize GetAlignedSize(VkDeviceSize size);
};