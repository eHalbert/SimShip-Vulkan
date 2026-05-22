/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Structures.h"
#include "Camera.h"
#include "Utility.h"
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "vulkan_texture.hpp"

// 2. LIB
#include <vulkan/vulkan.h>

// glm
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
using namespace glm;

extern "C"
{
#include "nova/nova.h"
}
#ifdef _DEBUG
#pragma comment(lib, "nova/Debug/nova.lib")
#else
#pragma comment(lib, "nova/Release/nova.lib")
#endif

// 3. WIN
#define NOMINMAX
#include <complex>
#include <vector>
using namespace std;


struct sHM
{
	int hour;
	int minute;
	int timezoneOffsetHours;
};


class Sky
{
public:
	Sky(shared_ptr<VulkanDevice>& vulkanDevice, vec2 pos, int width, int height);
	~Sky();

	// Render on screen (Bruneton 2008)
	void	CreatePipeline1(VkRenderPass renderPassScene, VkExtent2D extent);
	void	Render1(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera);
	
	void	ComputeSkyImageBruneton(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera);
	VulkanTexture* GetSkyImageBruneton(uint32_t currentFrame) { return mSkyImage2[currentFrame].get(); };

	// Render on screen (Sakmary 2022)
	void	CreatePipeline3(VkRenderPass renderPassScene, VkExtent2D extent);
	void	Render3(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera);
	
	// Render off screen (Sakmary 2022)
	void	ComputeSkyImageSakmary(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera);
	VulkanTexture* GetSkyImageSakmary(uint32_t currentFrame) { return mSkyImage4[currentFrame].get(); };

	float	GetRayleighDensity() const { return mAtmoParams.rayleigh_density.layers[1].exp_scale; }
	void	SetRayleighDensity(float value) { mAtmoParams.rayleigh_density.layers[1].exp_scale = value; bAtmoHasChanged = true; }
	
	float	GetMieDensity() const { return mAtmoParams.mie_density.layers[1].exp_scale; }
	void	SetMieDensity(float value) { mAtmoParams.mie_density.layers[1].exp_scale = value; bAtmoHasChanged = true; }

	void	SetObserver(vec2 pos);

	void	SetNow();
	sHM		GetNow();
	void	SetTime(int hour, int minute);
	sHM		GetTime();


	float	SunAzimut			= 0.0f;					// 0 = z-, 180 = z+ (anticlockwise)
	float	SunElevation		= 30.0f;				// 0 = y+, 180 = y- (clockwise)
	float	SunPctElevation		= 0.0f;					// % of sun elevation (0 = sun rise and sunset, 1 = sun at zenith)
	float	SunDistance			= 20000.0f;

	int		SunHour				= 12;					// Store the hours
	int		SunMinute			= 0;					// Store the minutes
	tm		tmTimeStored;

	float	Longitude			= -2.94097114f;
	float	Latitude			= 47.38162231f;

	vec3	SunPosition			= vec3(0.0f, 0.0f, 0.0f);
	vec3	SunDirection		= vec3(0.0f, 0.0f, 0.0f);

	vec3	SunEmissive			= vec3(1.0f, 0.98f, 0.92f);
	vec3	SunAmbient			= vec3(0.75f, 0.75f, 0.75f);
	vec3	SunDiffuse			= vec3(1.0f, 0.98f, 0.92f);
	vec3	SunSpecular			= vec3(1.0f, 0.98f, 0.92f);

	float	MieG				= 0.9f;
	float	SunIntensity		= 10.0f;

	float	MistDensity			= 0.00005f;
	float	FogDensity			= 0.0f;
	vec3	FogColor			= vec3(0.55f, 0.65f, 0.8f);
	float	StoreFogDensity		= 0.0f;
	float	StoreMistDensity	= 0.0f;
	bool    bVisible			= true;
	bool	bRain				= false;
	bool	bRainDropsTrails	= false;
	bool	bRainBlurDrips		= false;

	float	Exposure			= 1.0f;

	float	MoonAzimut			= 0.0f;					// 0 = z-, 180 = z+ (anticlockwise)
	float	MoonElevation		= 30.0f;				// 0 = y+, 180 = y- (clockwise)
	vec3	MoonDirection		= vec3(0.0f, 0.0f, 0.0f);
	float	MoonPhase			= 0.0f;
	float	MoonIntensity		= 1.0f;

	bool	bAtmoHasChanged		= false;

private:
	void	UpdateData(ln_date& date);

	int							mWidth, mHeight;
	Almanac						mAlmanac;

	shared_ptr<VulkanDevice>	mVulkanDevice;

	// BRUNETON 2008 ////////////////////////////////////////////

	sPipeline_x					mPipeline1;
	VkBuffer					mVertexBuffer;		// Screen quad
	VkDeviceMemory				mVertexBufferMemory;
	unique_ptr<VulkanTexture> 	mTexInscatterLUT;
	unique_ptr<VulkanTexture> 	mTexTransmittanceLUT;
	struct sSkyUBO
	{
		mat4        invProj;
		mat4        invView;
		vec3        camera;
		float		mieG;
		vec3        sunDir;
		float		sunIntensity;
	};

	void	CreateTextures1();
	void	CreateVertexBuffer();
	void	CreateDescriptors1();
	void	UpdateDescriptors1();
	float	GetMieG() { return glm::mix(0.86f, 0.97f, SunPctElevation); }

	sPipeline_x							mPipeline2;
	vector<unique_ptr<VulkanTexture>>	mSkyImage2;
	void	CreatePipeline2();
	void	CreateDescriptors2();
	void	UpdateDescriptors2();

	// SAKMARY 2022 ////////////////////////////////////////////
	
	struct DensityProfileLayer {
		float width;
		float exp_term;
		float exp_scale;
		float linear_term;
		float constant_term;
	};
	struct DensityProfile {
		DensityProfileLayer layers[2];
	};
	struct AtmosphereParameters {
		// The solar irradiance at the top of the atmosphere.
		vec3 solar_irradiance;
		// The sun's angular radius. Warning: the implementation uses approximations that are valid only if this angle is smaller than 0.1 radians.
		float sun_angular_radius;
		// The distance between the planet center and the bottom of the atmosphere.
		float bottom_radius;
		// The distance between the planet center and the top of the atmosphere.
		float top_radius;
		// The density profile of air molecules, i.e. a function from altitude to dimensionless values between 0 (null density) and 1 (maximum density).
		DensityProfile rayleigh_density;
		// The scattering coefficient of air molecules at the altitude where their density is maximum (usually the bottom of the atmosphere), as a function of wavelength. The scattering coefficient at altitude h is equal to 'rayleigh_scattering' times 'rayleigh_density' at this altitude.
		vec3 rayleigh_scattering;
		// The density profile of aerosols, i.e. a function from altitude to dimensionless values between 0 (null density) and 1 (maximum density).
		DensityProfile mie_density;
		// The scattering coefficient of aerosols at the altitude where their density is maximum (usually the bottom of the atmosphere), as a function of wavelength. The scattering coefficient at altitude h is equal to 'mie_scattering' times 'mie_density' at this altitude.
		vec3 mie_scattering;
		// The extinction coefficient of aerosols at the altitude where their density is maximum (usually the bottom of the atmosphere), as a function of wavelength. The extinction coefficient at altitude h is equal to 'mie_extinction' times 'mie_density' at this altitude.
		vec3 mie_extinction;
		// The asymetry parameter for the Cornette-Shanks phase function for the aerosols.
		float mie_phase_function_g;
		// The density profile of air molecules that absorb light (e.g. ozone), i.e. a function from altitude to dimensionless values between 0 (null density) and 1 (maximum density).
		DensityProfile absorption_density;
		// The extinction coefficient of molecules that absorb light (e.g. ozone) at The extinction coefficient at altitude h is equal to 'absorption_extinction' times 'absorption_density' at this altitude.
		vec3 absorption_extinction;
		// The average albedo of the ground.
		vec3 ground_albedo;
		// The cosine of the maximum Sun zenith angle for which atmospheric scattering must be precomputed (for maximum precision, use the smallest Sun zenith angle yielding negligible sky light radiance values. For instance, for the Earth case, 102 degrees is a good choice - yielding mu_s_min = -0.2).
		float mu_s_min;
	};
	struct sAtmosphereUBO
	{
		alignas(16)	vec3	solar_irradiance;
		alignas(4)	float	sun_angular_radius;

		alignas(16) vec3	absorption_extinction;

		alignas(16) vec3	rayleigh_scattering;
		alignas(4)	float	mie_phase_function_g;

		alignas(16) vec3	mie_scattering;
		alignas(4)	float	bottom_radius;

		alignas(16) vec3	mie_extinction;
		alignas(4)	float	top_radius;

		alignas(16) vec3	mie_absorption;
		alignas(16) vec3	ground_albedo;

		alignas(16) float	rayleigh_density[12];
		alignas(16) float	mie_density[12];
		alignas(16) float	absorption_density[12];

		alignas(8)	vec2	TransmittanceTexDimensions;
		alignas(8)	vec2	MultiscatteringTexDimensions;
		alignas(8)	vec2	SkyViewTexDimensions;

		alignas(16) vec3	sunDirection;
		alignas(16) vec3	cameraPosition;
		alignas(16)	vec3	moonDirection;
		alignas(4)	float	moonPhase;
		alignas(4)	float	moonIntensity;
	};
	AtmosphereParameters	mAtmoParams;
	void InitAtmosphere();
	unique_ptr<VulkanUBO>	mAtmoUBO;
	void UpdateAtmosphereUBO(sAtmosphereUBO* buffer, Camera& camera);

	vec3					mLastSunDirection		= vec3(0.0f);
	float					mLUTRecomputeThreshold	= 0.001f; // angle ~0.05°

	struct sCommonUBO
	{
		alignas(16) mat4	model;
		alignas(16) mat4	view;
		alignas(16) mat4	proj;
		alignas(4)	float	time;
		alignas(16) mat4	lHviewProj;
	};
	unique_ptr<VulkanUBO>		mCommonUBO;

	sPipeline_1					mTransmittanceLUTPipeline;
	unique_ptr<VulkanTexture>	mTransmittanceLUTImage;
	void CreateTransmittanceLUTPipeline();
	void CreateTransmittanceLUTDescriptors();
	void UpdateTransmittanceLUTDescriptors();

	sPipeline_1					mMultiscatteringLUTPipeline;
	unique_ptr<VulkanTexture>	mMultiscatteringLUTImage;
	void CreateMultiscatteringLUTPipeline();
	void CreateMultiscatteringLUTDescriptors();
	void UpdateMultiscatteringLUTDescriptors();

	sPipeline_1					mSkyViewLUTPipeline;
	unique_ptr<VulkanTexture>	mSkyViewLUTImage;
	VkSampler					mSkyViewLutSampler;
	void CreateSkyViewLUTPipeline();
	void CreateSkyViewLUTDescriptors();
	void UpdateSkyViewLUTDescriptors();

	void ComputeLUTsSakmary2022(Camera& camera);

	sPipeline_x						mPipeline3;
	vector<unique_ptr<VulkanUBO>>	mCommonUBO3;
	vector<unique_ptr<VulkanUBO>>	mAtmoUBO3;

	void CreateDescriptors3();
	void UpdateDescriptors3();

	sPipeline_x							mPipeline4;
	vector<unique_ptr<VulkanTexture>>	mSkyImage4;
	vector<unique_ptr<VulkanUBO>>		mCommonUBO4;
	vector<unique_ptr<VulkanUBO>>		mAtmoUBO4;
	void CreatePipeline4();
	void CreateDescriptors4();
	void UpdateDescriptors4();
};
