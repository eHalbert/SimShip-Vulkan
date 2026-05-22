/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "vulkan_ubo.hpp"

VulkanUBO::VulkanUBO(shared_ptr<VulkanDevice>& vulkanDevice, VkDeviceSize size, VkBufferUsageFlags usage)
{
    mVulkanDevice   = vulkanDevice;
    mSize           = GetAlignedSize(size);

    CreateBuffer(usage);
    vkMapMemory(mVulkanDevice->device, memory, 0, size, 0, &data);
	HasCoherentMemory();
}
VulkanUBO::~VulkanUBO()
{
   if (!mVulkanDevice->device)
	   return;

    if (data)
    {
        vkUnmapMemory(mVulkanDevice->device, memory);
        data = nullptr;
    }
    vkDestroyBuffer(mVulkanDevice->device, buffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, memory, nullptr);
}
void VulkanUBO::Flush()
{
    // Si mémoire NON cohérente ? flush manuel requis
    if (!hasCoherentMemory)
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.size = mSize;  // Flush toute la région

        vkFlushMappedMemoryRanges(mVulkanDevice->device, 1, &mappedRange);
    }
    // Si hasCoherentMemory = true ? automatique, rien à faire
}

void VulkanUBO::CreateBuffer(VkBufferUsageFlags usage)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = mSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(mVulkanDevice->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        throw runtime_error("Failed to create buffer!");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(mVulkanDevice->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        throw runtime_error("Failed to allocate buffer memory!");

    vkBindBufferMemory(mVulkanDevice->device, buffer, memory, 0);
}
uint32_t VulkanUBO::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mVulkanDevice->physicalDevice, &memProperties);

    // 1. PRIORITÉ: HOST_CACHED + HOST_VISIBLE + HOST_COHERENT (optimal)
    VkMemoryPropertyFlags ideal = properties | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
        ideal |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & ideal) == ideal)
            return i;

    // 2. Fallback: HOST_VISIBLE + HOST_COHERENT (standard)
    ideal = properties | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & ideal) == ideal)
            return i;

    // 3. ULTIME fallback (peut nécessiter flush)
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    throw runtime_error("failed to find suitable memory type!");
}
void VulkanUBO::HasCoherentMemory()
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mVulkanDevice->physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = GetMemoryTypeIndex();
    VkMemoryType& memoryType = memProperties.memoryTypes[memoryTypeIndex];

    hasCoherentMemory = (memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}
uint32_t VulkanUBO::GetMemoryTypeIndex()
{
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(mVulkanDevice->device, buffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mVulkanDevice->physicalDevice, &memProperties);

    // Trouve l'index du type de mémoire associé à cette allocation
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((memRequirements.memoryTypeBits & (1 << i)) != 0)
        {
            // Vérifie que les propriétés correspondent exactement à notre allocation
            VkMemoryPropertyFlags typeProps = memProperties.memoryTypes[i].propertyFlags;
            VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            if ((typeProps & requiredProps) == requiredProps)
                return i;
        }
    }

    throw runtime_error("Could not determine memory type index for existing allocation!");
}
VkDeviceSize VulkanUBO::GetAlignedSize(VkDeviceSize size)
{
    return (size + mVulkanDevice->minOffsetAlignment - 1) & ~(mVulkanDevice->minOffsetAlignment - 1);
}
