// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

// 3. WIN
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <algorithm>
using namespace std;


class VulkanSwapChain
{
    public:
        VkSwapchainKHR      swapChain;
        VkFormat            imageFormat;
        VkExtent2D          extent;
        uint32_t            imageCount;
        uint32_t            minImageCount;
        vector<VkImageView> vImageViews;
        vector<VkImage>     swapChainImages;

        VulkanSwapChain(shared_ptr<VulkanDevice>& device, int width, int height);
        ~VulkanSwapChain();

    private:
        VkSurfaceKHR        surface;
        shared_ptr<VulkanDevice> device;    


        VkExtent2D          chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, int width, int height);
        VkPresentModeKHR    chooseSwapPresentMode(const vector<VkPresentModeKHR> &availablePresentModes);
        VkSurfaceFormatKHR  chooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR> &availableFormats);
};