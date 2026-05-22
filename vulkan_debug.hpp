// Inspired from Matej Sakmary, https://github.com/MatejSakmary, Licensed under Apache License Version 2.0

#pragma once

// 1. PROJET

// 2. LIB
#include <vulkan/vulkan.h>

// 3. WIN
#include <iostream>
#include <vector>
#include <string>
using namespace std;


#ifdef _DEBUG
const bool              g_bEnableValidationLayers = true;
#else
const bool              g_bEnableValidationLayers = false;
#endif
const vector<const char*> g_vValidationLayers = { "VK_LAYER_KHRONOS_validation" };

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
bool checkValidationLayerSupport();
void setupDebugMessenger(VkInstance instance);
void cleanupDebugMessenger(VkInstance instance);
void SetDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name);