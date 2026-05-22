/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

// Adapted from Federico Vaccaro

#pragma once

// 1. PROJET
#include "Camera.h"
#include "Sky.h"

// 2. LIB
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// 3. WIN
#include <random>

using namespace std;
using namespace glm;

class Clouds
{
public:
	Clouds(shared_ptr<VulkanDevice>& vulkanDevice, VkExtent2D extent, float windSpeedKN);
	~Clouds();

	void InitVariables();
	void SetCloudSpeed(float windSpeedKN);
	
	void ComputeLUTs();
	void ComputeNewWeatherLUT();

	// Render on screen
	void CreateOnScreenPipeline(VkRenderPass renderPassScene, VkExtent2D extent);
	void Render(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec2 wind);

	// Render off screen
	void ComputeOffScreenImage(VkCommandBuffer cmd,uint32_t currentFrame, Camera& camera, Sky* sky, vec2 wind);
	void CreatePostPipeline(VkRenderPass renderPassScene, VkExtent2D extent);
	void UpdatePostDescriptors(VulkanTexture* skyTexture);
	void RenderPost(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky);
	VkImage GetOutputImage(uint32_t currentFrame) { return mCloudsImage[currentFrame]->image; }

	float	Coverage = 0.0f;
	float	CloudSpeed = 0.0f;
	float	Crispiness = 0.0f;
	float	Curliness = 0.0f;
	float	Density = 0.0f;
	float	Illumination = 1.8f;
	float	Absorption = 0.0f;
	float	SphereInnerRadius = 0.0f;
	float	SphereOuterRadius = 0.0f;
	float	PerlinFrequency = 0.0f;
	bool	bPostProcess = true;

	vec3	CloudColorTop = vec3(0.0f);
	vec3	CloudColorBottom = vec3(0.0f);

	vec3	Seed = vec3(0.0f);
	bool    bVisible = true;

	bool	PostDescriptorsInitialized = false;
private:

	shared_ptr<VulkanDevice>	mVulkanDevice;
	uint32_t                    mFramesInFlight;
	VkExtent2D					mSwapChainExtent;

	// LUTs
	VulkanTexture 			mTexPerlin;
	sPipeline_1				mPipelinePerlin;
	void CreatePerlinPipeline();
	void CreatePerlinDescriptors();
	void UpdatePerlinDescriptors();

	VulkanTexture 			mTexWorley;
	sPipeline_1				mPipelineWorley;
	void CreateWorleyPipeline();
	void CreateWorleyDescriptors();
	void UpdateWorleyDescriptors();

	VulkanTexture 			mTexWeather;
	sPipeline_1				mPipelineWeather;
	struct sWeatherUBO
	{
		vec3    seed = vec3(0.0f);
		float	perlinAmplitude = 0.5f;
		float	perlinFrequency = 0.8f;
		float	perlinScale = 100.0f;
		int		perlinOctaves = 4;
		float	pad = 0.0f;
	};
	void CreateWeatherPipeline();
	void CreateWeatherDescriptors();
	void UpdateWeatherDescriptors();

	struct sCloudsUBO {
		vec2 iResolution;
		float FOV;
		float iTime;

		mat4 inv_view;
		mat4 inv_proj;
		mat4 invViewProj;

		vec3 cameraPosition;
		float coverage_multiplier;

		vec3 lightColor;
		float cloudSpeed;

		vec3 lightDirection;
		float crispiness;

		vec3 wind;
		float illumination;

		vec3 cloudColorTop;
		float curliness;

		vec3 cloudColorBottom;
		float absorption;

		float densityFactor;
		float sphereInnerRadius;
		float sphereOuterRadius;
		float exposure;
	};
	sPipeline_x					mPipelineClouds1;
	void CreateOnScreenDescriptors();
	void UpdateOnScreenDescriptors(uint32_t currentFrame);

	sPipeline_x					mPipelineClouds2;
	vector<unique_ptr<VulkanTexture>>mCloudsImage;
	void CreateOffScreenPipeline();
	void CreateOffScreenDescriptors();
	void UpdateOffScreenDescriptors();

	sPipeline_x					mPipelinePost;
	VkSampler					mSampler;
	void CreatePostDescriptors();
	struct sCloudsPostUBO
	{
		vec2	cloudsResolution;
		int		bShowSky;
		int		bShowClouds;
	};
};