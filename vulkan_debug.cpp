// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#include <iostream>

#include "vulkan_debug.hpp"

extern VkDebugUtilsMessengerEXT_T* g_DebugMessenger;


VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    wcerr << L"[Validation] ";

    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: wcerr << L"VERBOSE: "; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: wcerr << L"INFO: "; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: wcerr << L"WARNING: "; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: wcerr << L"ERROR: "; break;
    }

    wcerr << pCallbackData->pMessage << endl;
    return VK_FALSE;
}
bool checkValidationLayerSupport()
{
    // Check if the validation layers are available

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : g_vValidationLayers)
    {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
            return false;
    }
    return true;
}
void setupDebugMessenger(VkInstance instance)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        func(instance, &createInfo, nullptr, &g_DebugMessenger);
    else
        throw runtime_error("Failed to create debug messenger");

#ifdef INFO_INIT    // Structures.h
    wcout << L"Debug messenger OK\n";
#endif
}
void cleanupDebugMessenger(VkInstance instance)
{
    if (g_DebugMessenger != VK_NULL_HANDLE)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
            func(instance, g_DebugMessenger, nullptr);
        g_DebugMessenger = VK_NULL_HANDLE;
    }
}
void SetDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
{
    // Helper function to name any Vulkan object

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;

    // Load the function pointer once (extension not in core)
    static auto fn = (PFN_vkSetDebugUtilsObjectNameEXT)
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (fn) fn(device, &info);
}