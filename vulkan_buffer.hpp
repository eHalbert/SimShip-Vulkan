// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"

// 2. LIB
#include <vulkan/vulkan.h>

// 3. WIN
#include <memory>
#include <stdexcept>
#include <iostream>
using namespace std;


class VulkanBuffer
{
    public:
        shared_ptr<VulkanDevice>    device;
        VkBuffer                    buffer;
        VkDeviceMemory              bufferMemory;

        VulkanBuffer(shared_ptr<VulkanDevice> device ,VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

        ~VulkanBuffer();
        void CopyIntoBuffer(VulkanBuffer &srcBuffer, VkDeviceSize size);
        void CopyFromCPU(const void* data, VkDeviceSize size);
};