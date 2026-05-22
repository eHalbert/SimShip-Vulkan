// Inspired from Matěj Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#include "vulkan_swapchain.hpp"
#include "Utility.h"

VulkanSwapChain::VulkanSwapChain(shared_ptr<VulkanDevice>& device, int width, int height) : device{device}, surface{ device->surface}
{
    SwapChainSupportDetails details = device->querySwapChainSupport(device->physicalDevice, surface);
    // It is recommended to request at least one more than minimum so we don't have to wait for driver to complete internal operations before aquiring another image
    minImageCount = details.capabilities.minImageCount;
    imageCount = details.capabilities.minImageCount;
    // Make sure not to exceed the maximum. NOTE: 0 is special value for "there is no maximum"
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount)
        imageCount = details.capabilities.maxImageCount;
    
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(details.presentModes);
    VkExtent2D extent = chooseSwapExtent(details.capabilities, width, height);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    // specifies the amount of layers image consist of
    createInfo.imageArrayLayers = 1;
    // specify that we want to render directly into the image -> used as color attachment
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // specify how to handle swap chain images used across multiple queue families
    uint32_t queueFamilyIndices[] = {device->familyIndices.graphicsFamily.value(), device->familyIndices.presentFamily.value()};

    if (device->familyIndices.graphicsFamily != device->familyIndices.presentFamily)
    {
        // image can be used across multiple queue families without explicit ownership transfers
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        // image in swap chain is is owned by one family and must be explicitly transfered before using it in another family
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // OPTIONAL
        createInfo.pQueueFamilyIndices = nullptr;
    }

    // transform applied to image -> 90° rotation or horizontal flip
    createInfo.preTransform = details.capabilities.currentTransform;
    // specifies if alpha channel should be used for blending with other windows in the system -> in most cases we want to simply ignore
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    // We don't care about pixels that are obscured f.e. by another window
    createInfo.clipped = VK_TRUE;
    // When the window f.e. gets resized we need to create new swap chain and point it to the old one
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(device->device, &createInfo, nullptr, &swapChain);
    if (result != VK_SUCCESS)
    {
        cout << "Result is " << result << endl;
        throw runtime_error("VULKAN_SWAP_CHAIN::CREATE_SWAPCHAIN::Failed to create swapchain");
    }

    // Previously we only specified min number of images, there could be more
    vkGetSwapchainImagesKHR(device->device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device->device, swapChain, &imageCount, swapChainImages.data());
    imageFormat = surfaceFormat.format;
    this->extent = extent;

    vImageViews.resize(swapChainImages.size());
    for(size_t i = 0; i < swapChainImages.size(); i++)
        vImageViews[i] = CreateImageView(device->device, swapChainImages[i], imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

	device->PreferredTextureFormat = imageFormat;
}
VulkanSwapChain::~VulkanSwapChain()
{
    //for (auto imageView : vImageViews)
    //    vkDestroyImageView(device->device, imageView, nullptr);

    //vkDestroySwapchainKHR(device->device, swapChain, nullptr);
}

VkSurfaceFormatKHR VulkanSwapChain::chooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR> &availableFormats)
{
    // Priorité 1 : UNORM = comportement OpenGL classique, pas de correction gamma auto
    for (const auto& f : availableFormats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;

    // Priorité 2 : variante R/B swappée
    for (const auto& f : availableFormats)
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;

    return availableFormats[0];
}
VkPresentModeKHR VulkanSwapChain::chooseSwapPresentMode(const vector<VkPresentModeKHR>& availablePresentModes)
{
    // With 3+ images, prefer Mailbox: low latency + no tearing
    if (imageCount >= 3)
    {
        for (const auto& mode : availablePresentModes)
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                return mode;
    }

    // With 2 images, Immediate gives lowest latency (tearing possible)
    for (const auto& mode : availablePresentModes)
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return mode;

    // FIFO is always guaranteed as fallback
    return VK_PRESENT_MODE_FIFO_KHR;
}
VkExtent2D VulkanSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, int width, int height)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        //cout << "Matching Swap extent to the resolution of the window " << capabilities.currentExtent.width << " " << capabilities.currentExtent.height << endl;

        return capabilities.currentExtent;
    }
    else
    {
        VkExtent2D actualExtent =
        {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // clamp to fit into supported dimensions
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

