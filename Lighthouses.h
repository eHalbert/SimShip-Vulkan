/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"
#include "vulkan_device.hpp"
#include "vulkan_ubo.hpp"
#include "Camera.h"
#include "Light.h"
#include "Beam.h"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;
#include "pugixml/pugixml.hpp"
#ifdef _DEBUG
#pragma comment(lib, "pugixml/Debug/pugixml.lib")
#else
#pragma comment(lib, "pugixml/Release/pugixml.lib")
#endif

// 3. WIN
#include <array>
#include <memory>
#include <random>
#include <ctime> 
using namespace std;

#pragma once

enum eLightType { beam = 0, light };	// Flash, Occultation
struct sLighthouse
{
	wstring			name;
	vec3			pos;
	float			range;
	eLightType		type;
	vector<float>	vAngles;
	vector<vec3>	vColors;
	float			durationOfTurn;
	double			angleStart;
	bool			isInViewFrustum;
	Beam *			beam;
};

class Lighthouses
{
public:
	Lighthouses(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const wstring filename);
	~Lighthouses();

	void LoadFromXML(const wstring filename);
	
	void RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights = false);
	void RenderBeams(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights = false);

	void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

	bool						bVisible = true;

private:
	vec3 GetSectorColor(sLighthouse& lh, float angleDeg);

	shared_ptr<VulkanDevice>    mVulkanDevice;
	VkRenderPass				mRenderPass;
	VkExtent2D					mExtent;

	vector<sLighthouse>			mvLighthouses;

	unique_ptr<Light>			mLight;
};

