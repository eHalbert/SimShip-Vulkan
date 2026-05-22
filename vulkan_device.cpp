// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#include "vulkan_device.hpp"

const vector<const char*> vDeviceExtensions = {
    "VK_KHR_swapchain",
    "VK_KHR_spirv_1_4",
    "VK_KHR_create_renderpass2",
    "VK_KHR_depth_stencil_resolve",
    "VK_KHR_dynamic_rendering",
    "VK_NV_mesh_shader",
    "VK_KHR_maintenance4",
    "VK_EXT_extended_dynamic_state3"
};

VulkanDevice::VulkanDevice(const VkInstance &instance, GLFWwindow* hWindow)
{
    glfwCreateWindowSurface(instance, hWindow, nullptr, &surface);

    pickPhysicalDevice(instance, surface);
    createLogicalDevice(surface);
    createCommandPool();
}
VulkanDevice::~VulkanDevice()
{
}

void VulkanDevice::pickPhysicalDevice(const VkInstance &instance, const VkSurfaceKHR surface)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
        throw runtime_error("VULKAN_DEVICE::PICK_PHYSICAL_DEVICE::failed to find GPUs with Vulkan support");

    vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto &device : devices)
    {
        if (isDeviceSuitable(device, surface))
        {
            physicalDevice = device;
            msaaSamples = getMaxUsableSampleCount();
            familyIndices = findQueueFamilies(device, surface);

            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
            minOffsetAlignment = deviceProperties.limits.minUniformBufferOffsetAlignment;
            timestampPeriod = deviceProperties.limits.timestampPeriod;  // ns per tick

            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
        throw runtime_error("VULKAN_DEVICE::PICK_PHYSICAL_DEVICE::failed to find a suitable GPU");

    /*
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    cout << "Physical device " << properties.deviceName << " timestamp period is " << properties.limits.timestampPeriod << endl;
    uint32_t maxDescriptorSets = properties.limits.maxBoundDescriptorSets;
    uint32_t maxPerStageUniformBuffers = properties.limits.maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageSamplers = properties.limits.maxPerStageDescriptorSamplers;
    cout << "Physical device maxPushConstantsSize is " << properties.limits.maxPushConstantsSize << " bytes" << endl;
    VkDeviceSize minUniformBufferOffsetAlignment = properties.limits.minUniformBufferOffsetAlignment;
    */
}
bool VulkanDevice::isDeviceSuitable(const VkPhysicalDevice device, const VkSurfaceKHR surface)
{
    QueueFamilyIndices indices = findQueueFamilies(device, surface);

    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapChainAdequate = false;

    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    // Querry for device features and check if anisotropic filtering is supported by device
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && 
        supportedFeatures.samplerAnisotropy && 
        supportedFeatures.vertexPipelineStoresAndAtomics &&
        supportedFeatures.largePoints;
}
QueueFamilyIndices VulkanDevice::findQueueFamilies(const VkPhysicalDevice device, const VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies)
    {
        // Check for support of presentation capability
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;
        if (presentSupport)
            indices.presentFamily = i;
        if(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            if (queueFamily.timestampValidBits == 0)
                throw runtime_error("VKDEVICE::FIND_QUEUE_FAMILIES::Timestamps not valid for compute shaders");

            indices.computeFamily = i;
        }
        if (indices.isComplete())
            break;
        i++;
    }

    return indices;
}
bool VulkanDevice::checkDeviceExtensionSupport(const VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    set<string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions)
        requiredExtensions.erase(extension.extensionName);

    return requiredExtensions.empty();
}
SwapChainSupportDetails VulkanDevice::querySwapChainSupport(const VkPhysicalDevice device, const VkSurfaceKHR surface)
{
    SwapChainSupportDetails details;

    // query for basic surface capabilities -> takes into account physicalDevice and surface
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        // resize the vector since we are using it as an array
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        // resize the vector since we are using it as an array
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}
VkSampleCountFlagBits VulkanDevice::getMaxUsableSampleCount()
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts =  physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
    return VK_SAMPLE_COUNT_1_BIT;
}
void VulkanDevice::createLogicalDevice(const VkSurfaceKHR surface)
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

    // Queues
    vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value(), indices.computeFamily.value() };
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // deviceFeatures2 → hostQueryReset → maintenance4 → dynamicState3 → demoteFeatures → nullptr
    
    // Discard/demote
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteFeatures{};
    demoteFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    demoteFeatures.shaderDemoteToHelperInvocation = VK_TRUE;

    // Dynamic polygon mode
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3{};
    dynamicState3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    dynamicState3.extendedDynamicState3PolygonMode = VK_TRUE;
    dynamicState3.pNext = &demoteFeatures;
    
    // Maintenance4
    VkPhysicalDeviceMaintenance4Features maintenance4{};
    maintenance4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;
    maintenance4.maintenance4 = VK_TRUE;
    maintenance4.pNext = &dynamicState3;

    // HostQueryReset
    VkPhysicalDeviceHostQueryResetFeatures hostQueryReset{};
    hostQueryReset.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
    hostQueryReset.hostQueryReset = VK_TRUE;
    hostQueryReset.pNext = &maintenance4;

    // Features2 (racine)
    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &hostQueryReset;

    // Features legacy
    deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    deviceFeatures2.features.sampleRateShading = VK_TRUE;
    deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
    deviceFeatures2.features.shaderClipDistance = VK_TRUE;
    deviceFeatures2.features.depthClamp = VK_TRUE;
    deviceFeatures2.features.largePoints = VK_TRUE;

    // pFeatures2 instead of pEnabledFeatures + pNext
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceInfo.pNext = &deviceFeatures2;

    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(vDeviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = vDeviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
        throw runtime_error("Failed to create logical device");

    // CHARGEMENT DES FONCTIONS DYNAMIQUES
    vkCmdSetPolygonModeEXT = (PFN_vkCmdSetPolygonModeEXT)vkGetDeviceProcAddr(device, "vkCmdSetPolygonModeEXT");
    if (!vkCmdSetPolygonModeEXT)
        throw runtime_error("Failed to load vkCmdSetPolygonModeEXT");

    vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)vkGetDeviceProcAddr(device, "vkCmdSetLineWidth");
    if (!vkCmdSetLineWidth)
        std::cerr << "Warning: vkCmdSetLineWidth not available" << std::endl;

    // Queues
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    vkGetDeviceQueue(device, indices.computeFamily.value(), 0, &computeQueue);
}
VkFormat VulkanDevice::findSupportedFormat(const vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{ 
    for (VkFormat format : candidates)
    {
        /* Contains three fields
            linearTilingFeatures: Use cases that are supported with linear tiling
            optimalTilingFeatures: Use cases that are supported with optimal tiling
            bufferFeatures: Use cases that are supported for buffers */
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        
        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
            return format;
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
            return format;
    }
    throw runtime_error("VULKAN_DEVICE::FIND_SUPPORTED_FORMAT::Failed to find supported format");
}
void VulkanDevice::createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = familyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &graphicsCommandPool) != VK_SUCCESS)
        throw runtime_error("VULKAN_DEVICE::CREATE_COMMAND_POOL::Failed to create command pool");

    // Separate command pool as queue family for compute may be different than graphics
    VkCommandPoolCreateInfo cmdPoolInfo {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = familyIndices.computeFamily.value();
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS)
        throw runtime_error("VULKAN_DEVICE::CREATE_COMPUTE_COMMAND_POOL::Failed to create command pool");
}
VkCommandBuffer VulkanDevice::createGraphicsCommandBuffer()
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    /* PRIMARY -> can be submitted to a queue for execution but cannot be called from other buffers
       SECONDARY -> cannot be submitted directly, but can be called from primary buffers*/
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
        throw runtime_error("DEVICE::CREATE_COMMAND_BUFFERS::Failed to allocate graphics command buffer");

    return commandBuffer;
}
VkCommandBuffer VulkanDevice::createComputeCommandBuffer()
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = computeCommandPool;
    /* PRIMARY -> can be submitted to a queue for execution but cannot be called from other buffers
       SECONDARY -> cannot be submitted directly, but can be called from primary buffers*/
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
        throw runtime_error("DEVICE::CREATE_COMMAND_BUFFERS::Failed to allocate compute command buffer");

    return commandBuffer;
}
uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{ 
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // typeFilter is used to specify the bit field of memory types that are suitable -> we are iterating over them and checking if the corresponding bit is set
        if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw runtime_error("VULKAN_DEVICE::FIND_MEMORY_TYPE::Failed to find suitable memory type");
}
VkCommandBuffer VulkanDevice::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}
void VulkanDevice::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, graphicsCommandPool, 1, &commandBuffer);
}
vector<VkFormat> VulkanDevice::GetAllSupportedFormats()
{
    vector<VkFormat> vCoreVulkanFormats = 
    {
        // Packed 16-bit
        VK_FORMAT_R4G4_UNORM_PACK8,
        VK_FORMAT_R4G4B4A4_UNORM_PACK16,
        VK_FORMAT_B4G4R4A4_UNORM_PACK16,
        VK_FORMAT_R5G6B5_UNORM_PACK16,
        VK_FORMAT_B5G6R5_UNORM_PACK16,
        VK_FORMAT_R5G5B5A1_UNORM_PACK16,
        VK_FORMAT_B5G5R5A1_UNORM_PACK16,
        VK_FORMAT_A1R5G5B5_UNORM_PACK16,

        // 8-bit
        VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SNORM, VK_FORMAT_R8_UINT, VK_FORMAT_R8_SINT, VK_FORMAT_R8_SRGB,
        VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8_SRGB,

        // 24-bit RGB
        VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8_SRGB,
        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_B8G8R8_SNORM, VK_FORMAT_B8G8R8_UINT, VK_FORMAT_B8G8R8_SINT,

        // 32-bit RGBA
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_SINT, VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM, VK_FORMAT_B8G8R8A8_UINT, VK_FORMAT_B8G8R8A8_SINT,

        // Packed 32-bit
        VK_FORMAT_A8B8G8R8_UNORM_PACK32, VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        VK_FORMAT_A8B8G8R8_UINT_PACK32, VK_FORMAT_A8B8G8R8_SINT_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_SNORM_PACK32,
        VK_FORMAT_A2R10G10B10_UINT_PACK32, VK_FORMAT_A2R10G10B10_SINT_PACK32,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2B10G10R10_SNORM_PACK32,

        // 16-bit
        VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SNORM, VK_FORMAT_R16_UINT, VK_FORMAT_R16_SINT, VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_SFLOAT,

        // 48/64-bit
        VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SFLOAT,

        // 32-bit floats
        VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SFLOAT,

        // 64-bit+
        VK_FORMAT_R64_UINT, VK_FORMAT_R64_SINT, VK_FORMAT_R64_SFLOAT,
        VK_FORMAT_R64G64_UINT, VK_FORMAT_R64G64_SINT, VK_FORMAT_R64G64_SFLOAT,
        VK_FORMAT_R64G64B64_UINT, VK_FORMAT_R64G64B64_SINT, VK_FORMAT_R64G64B64_SFLOAT,
        VK_FORMAT_R64G64B64A64_UINT, VK_FORMAT_R64G64B64A64_SINT, VK_FORMAT_R64G64B64A64_SFLOAT,

        // Packed special
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,

        // Depth/stencil
        VK_FORMAT_D16_UNORM, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,

        // Compressed (BC)
        VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC1_RGB_SRGB_BLOCK,
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK,
        VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK,
        VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC4_SNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_BC5_SNORM_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK, VK_FORMAT_BC6H_SFLOAT_BLOCK,
        VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK,

        // ETC2/EAC
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
        VK_FORMAT_EAC_R11_UNORM_BLOCK, VK_FORMAT_EAC_R11_SNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_UNORM_BLOCK, VK_FORMAT_EAC_R11G11_SNORM_BLOCK,

        // ASTC (fin du core ~157-205)
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x4_UNORM_BLOCK, VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x5_UNORM_BLOCK, VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x5_UNORM_BLOCK, VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x6_UNORM_BLOCK, VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x5_UNORM_BLOCK, VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x6_UNORM_BLOCK, VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK, VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x5_UNORM_BLOCK, VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x6_UNORM_BLOCK, VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x8_UNORM_BLOCK, VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x10_UNORM_BLOCK, VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x10_UNORM_BLOCK, VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x12_UNORM_BLOCK, VK_FORMAT_ASTC_12x12_SRGB_BLOCK
    };
    vector<VkFormat> supported;

    for (auto format: vCoreVulkanFormats)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (props.linearTilingFeatures != 0 || props.optimalTilingFeatures != 0 || props.bufferFeatures != 0)
            supported.push_back(format);
    }
    return supported;
}
