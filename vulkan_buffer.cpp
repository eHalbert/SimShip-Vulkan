// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#include "vulkan_buffer.hpp"

VulkanBuffer::VulkanBuffer(shared_ptr<VulkanDevice> device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) : device{device}
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    // Just like images in swap chain buffers can be owned by specific queue family
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device->device, &bufferInfo, nullptr, &buffer);
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->device, buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits, properties);
    vkAllocateMemory(device->device, &allocInfo, nullptr, &bufferMemory);
    
    // Associate this memory with the buffer, 4th parameter -> offset within the region of memory
    vkBindBufferMemory(device->device, buffer, bufferMemory, 0);
}
VulkanBuffer::~VulkanBuffer()
{
    vkDestroyBuffer(device->device, buffer, nullptr);
    vkFreeMemory(device->device, bufferMemory, nullptr);
}

void VulkanBuffer::CopyIntoBuffer(VulkanBuffer &srcBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = device->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // OPTIONAL
    copyRegion.dstOffset = 0; // OPTIONAL
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer.buffer, buffer, 1, &copyRegion);

    device->EndSingleTimeCommands(commandBuffer);
}
void VulkanBuffer::CopyFromCPU(const void* data, VkDeviceSize size) 
{
    // 1. Create staging buffer (HOST VISIBLE)
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device->device, &stagingInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device->device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device->device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device->device, stagingBuffer, stagingMemory, 0);

    // 2. Copy CPU data to staging buffer
    void* stagingData;
    vkMapMemory(device->device, stagingMemory, 0, size, 0, &stagingData);
    memcpy(stagingData, data, size);
    vkUnmapMemory(device->device, stagingMemory);

    // 3. Copy from staging buffer to device buffer
    VkCommandBuffer cmd = device->BeginSingleTimeCommands();
    VkBufferCopy copyRegion{ 0, 0, size };
    vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copyRegion);
    device->EndSingleTimeCommands(cmd);

    // 4. Clean up staging buffer
    vkDestroyBuffer(device->device, stagingBuffer, nullptr);
    vkFreeMemory(device->device, stagingMemory, nullptr);
}