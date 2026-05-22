// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

// 3. WIN
#include <optional>
#include <vector>
#include <set>
#include <stdexcept>
#include <iostream>
using namespace std;


struct QueueFamilyIndices
{
    optional<uint32_t> graphicsFamily;
    optional<uint32_t> presentFamily;
    optional<uint32_t> computeFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value() && computeFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR    capabilities;
    vector<VkSurfaceFormatKHR>  formats;
    vector<VkPresentModeKHR>    presentModes;
};

/* Required extensions */
const vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_MESH_SHADER_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME };

class VulkanDevice
{
public:
    VkDevice                device;
    VkPhysicalDevice        physicalDevice;
    VkSampleCountFlagBits   msaaSamples;
    VkSurfaceKHR            surface;

    QueueFamilyIndices      familyIndices;
    VkCommandPool           graphicsCommandPool;
    VkCommandPool           computeCommandPool;

    VkQueue                 graphicsQueue;
    VkQueue                 presentQueue;
    VkQueue                 computeQueue;

    uint32_t                minOffsetAlignment;
    float                   timestampPeriod;
    VkFormat                PreferredTextureFormat;

    VulkanDevice(const VkInstance &instance, GLFWwindow* hWindow);
    ~VulkanDevice();

    VkCommandBuffer createGraphicsCommandBuffer();
    VkCommandBuffer createComputeCommandBuffer();
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    VkFormat findSupportedFormat(const vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice device, const VkSurfaceKHR surface);
    vector<VkFormat> GetAllSupportedFormats();

    PFN_vkCmdSetPolygonModeEXT getCmdSetPolygonModeEXT() const { return vkCmdSetPolygonModeEXT; }
	PFN_vkCmdSetLineWidth getCmdSetLineWidth() const { return vkCmdSetLineWidth; }

private:
    void pickPhysicalDevice(const VkInstance &instance, const VkSurfaceKHR surface);
    void createLogicalDevice(const VkSurfaceKHR surface);

    bool isDeviceSuitable(const VkPhysicalDevice device, const VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(const VkPhysicalDevice device);
    VkSampleCountFlagBits getMaxUsableSampleCount();
    QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice device, const VkSurfaceKHR surface);
    void createCommandPool();

    // Fonction pour polygon mode dynamique
    PFN_vkCmdSetPolygonModeEXT vkCmdSetPolygonModeEXT = nullptr;
    PFN_vkCmdSetLineWidth vkCmdSetLineWidth = nullptr;  // Optionnel pour épaisseur

};