/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Structures.h"
#include "vulkan_device.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#include <gli/gli/gli.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp>
using namespace glm;

// 3. WIN
#define WIN32_LEAN_AND_MEAN         // Exclude rarely used Windows headers
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <CommDlg.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>		// ofstream
#include <filesystem>
#include <iomanip>
#include <limits>
#include <corecrt_io.h>	// Console
#include <fcntl.h>		// Console
#include <vector>
#include <string>
#define _USE_MATH_DEFINES
#include <math.h>
using namespace std;


//#define INFO_INIT

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cout << "Fatal : VkResult is \"" << errorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
		assert(res == VK_SUCCESS);																		\
	}																									\
}
#ifndef SAFE_DELETE
#define SAFE_DELETE(p)       { if (p) { delete (p);     (p) = NULL; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p) = NULL; } }
#endif


// glm
void PrintGlmMatrix(mat4& mat, string name);
void PrintGlmVec3(vec3 vec, string name);
void PrintGlmVec3(vec3 vec);

vector<char> CompileShaderRuntime(const string& glslPath);
vector<char> ReadSPIRVFile(const string& filename);
VkShaderModule CreateShaderModule(VkDevice device, const vector<char>& code);

// Vulkan
struct QueueFamilyIndices_
{
    optional<uint32_t> graphicsFamily;
    optional<uint32_t> presentFamily;

    bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};
string errorString(VkResult errorCode);
VkFormat FindSupportedFormat(const vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat FindDepthFormat();
VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice physicalDevice);
uint32_t GetUniformBufferOffsetAlignment(VkPhysicalDevice physicalDevice);
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
void CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
void EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer);
void TransitionLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
void SaveDepthTexture2D(shared_ptr<VulkanDevice> device, VkImage image, int width, int height, string name);
VkSampler CreateTextureSamplerColor(VkDevice device);
float* ConvertRGBtoRGBA(const float* rgbData, uint32_t width, uint32_t height);
VkDescriptorSetLayout GetImGuiTextureDescriptorSetLayout(VkDevice device);
VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t depth);
GLFWmonitor* get_current_monitor(GLFWwindow* window);

// Save client area to image file (png)
wstring GetNextAvailableCaptureName(const wstring& folderPath);
void SaveHBITMAP(HBITMAP bitmap, HDC hDC, wchar_t* filename);
wstring SaveClientArea(HWND hwnd);

// Files
vector<string> ListFiles(const string& folder, const string& ext);

// Strings
string	wstring_to_utf8(const wstring& wstr);
wstring utf8_to_wstring(const string& str);
string Utf8ToAnsi(const string& utf8);

// Conversions
float ms_to_knot(float speedMS);
float knot_to_ms(float speedKnots);
float wind_to_dirdeg(vec2 windVector);
vec2 wind_from_speeddir(float directionDEG, float speedKN);

// Interpolations
quat	RotationBetweenVectors(vec3 A, vec3 B);
float	Sign(float value);
double	InterpolateAValue(const double start_1, const double end_1, const double start_2, const double end_2, double value_between_start_1_and_end_1);
bool	IsInRect(vec4& rect, vec2& point);
bool	IntersectionOfSegments(const vec2& p1, const vec2& p2, const vec2& p3, const vec2& p4, vec2& p);
bool	IntersectionOfSegments(const vec2& p1, const vec2& p2, const vec2& p3, const vec2& p4);

inline int MSToBeaufort(double v)
{
	int bf = (int)pow(v * v * 1.44, 0.33333);
	if (bf > 12)
		bf = 12;
	return bf;
}
template <class T> inline T WrapDEG(T angle)
{
	while (angle >= 360) angle -= (T)360;
	while (angle < -0) angle += (T)360;
	return angle;
}
template <class T> inline T	DifferenceDEG(T angle1, T angle2)
{
	angle1 = WrapDEG(angle1);
	angle2 = WrapDEG(angle2);
	if (abs(angle1 - angle2) >= 180.0)
		return WrapDEG(abs(360.0 - abs(angle1 - angle2)));
	else
		return WrapDEG(abs(angle1 - angle2));
}
template <class T> inline T MinDEG(T angle1, T angle2)
{
	angle1 = WrapDEG(angle1);
	angle2 = WrapDEG(angle2);
	if (abs(angle1 - angle2) >= 180.0)
		return max(angle1, angle2);
	else
		return min(angle1, angle2);
}
template <class T> inline T MaxDEG(T angle1, T angle2)
{
	angle1 = WrapDEG(angle1);
	angle2 = WrapDEG(angle2);
	if (abs(angle1 - angle2) >= 180.0)
		return min(angle1, angle2);
	else
		return max(angle1, angle2);
}
template <class T> inline int AreAnglesSorted(T angle1, T angle2)
{
	// return 
	// -1 : false (angle1 is superior to angle2)
	//  0 : anges are equal
	// +1 : angles are in the right order

	angle1 = WrapDEG(angle1);
	angle2 = WrapDEG(angle2);
	if (angle1 == angle2)
		return 0;

	T diff = DifferenceDEG(angle1, angle2);
	if (WrapDEG(angle1 + diff) == angle2)
		return 1;
	else
		return -1;
}

// Geography
float	lon_to_opengl(float lon);
float	lat_to_opengl(float lat);
vec3	lonlat_to_opengl(float lon, float lat);
vec2	opengl_to_lonlat(float x, float z);
float	get_angle_from_north(vec3 dir);
float   get_yaw_from_hdg(float hdgDeg);
float	get_hdg_from_yaw(float yawRad);
string	display_geographic_angle(float angle, int decimal);

// Colors
vec3 color_255_to_1(vec3 v);
void rgb_to_hsl(const vec3& rgb, float& h, float& s, float& l);
vec3 sRGB_to_linear(const vec3& c);
