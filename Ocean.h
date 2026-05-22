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
#include "vulkan_buffer.hpp"
#include "vulkan_ubo.hpp"
#include "Sky.h"
#include "Timer.h"
#include "Spectra.h"

// 2. LIB
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
using namespace glm;
#define FFTW_DLL
#include <fftw3/fftw3.h>
#include <stb/stb_image.h>

// 3. WIN
#define NOMINMAX
#include <complex>
#include <vector>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <numeric>
#include <random>
#include <thread>
#include <mutex>
using namespace std;


class Ocean
{
	struct sVertexOcean
	{
		vec3            position;
		vec2            texCoord;
	};
	struct sLODPatch
	{
		unique_ptr<VulkanBuffer>    vertexBuffer;
		unique_ptr<VulkanBuffer>    indexBuffer;
		uint32_t                    indexCount;
	};
	struct sInstanceData
	{
		mat4            modelMatrix;
		int             lod;                // 0-4
		float           padding[3];
	};
	struct sFrameData
	{
		unique_ptr<VulkanUBO>   ubo;
		uint32_t                instanceCount = 0;
	};
	struct alignas(16) PatchInfo {
		vec4 bboxMin;		// AABB min (x,y,z,w=padding)
		vec4 bboxMax;		// AABB max (x,y,z,w=padding)  
		ivec2 gridPos;		// Position dans grille 300x300 (x,z)
		float padding[2];   // Alignement 16 bytes

		static VkDescriptorSetLayoutBinding GetBinding() {
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = 0;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_TASK_BIT_NV;
			return binding;
		}
	};

public:

	Ocean(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
	~Ocean();

	void		Init(vec2 wind);
	void		InitFrequencies();
	void		SetWind(vec2 wind);
	void		EvaluatePersistence(float seconds);

	void		Update(float t, uint32_t currentFrame);

	bool		GetVerticeXZ(vec2 pos, vec3& output);
	bool		GetVerticeXYZ(vec3 pos, vec3& output);

	float	  * GetPixelsDisplacement() { return mPixelsDisplacements.get(); };

	const VulkanTexture&	GetDisplacements() const { return mTexDisplacements; }
	const VulkanTexture&	GetGradients() const { return mTexGradients; }
	const VulkanTexture&	GetFoamBuffer() const { return *mTexFoamBuffer; }

	void					GetRecordFromBuoy(vec2 pos, float t);
	void					ClearRecords();
	bool					GetWaveByWaveAnalysis(float& waves1_3, float& waveMax, int& nWaves, float& average_period);
	vector<vec2>			GetCut(int xN);
	double					GetSpectralMoment(const vector<double>& densiteSpectrale, double df, int ordre);
	pair<vector<double>, vector<double>> GetFrequencies();
	vector<sResultData>		SpectralAnalysis();
	vector<sResultData>		DirectionalAnalysis();

	void		RenderWireframe(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera);
	void		RenderOneMesh(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec3& ShipPosition, float ShipRotation);
	void		RenderInstancedMeshs(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky);
	void		RenderFull(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec3& ShipPosition, float ShipRotation, bool bKelvinWakes, float LWL, float kelvinScale, float shipVelocity, float centerFore, int baseFroude);

	void		RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

	// Dimensions
	const int			FFT_SIZE		= 512;				// Dimension of the FFT (1024 max)
	const int			FFT_SIZE_1		= FFT_SIZE + 1;		 
	const int			MESH_SIZE		= 256;				// Dimension of the mesh (number of cells on one side of the grid)
	const int			MESH_SIZE_1		= MESH_SIZE + 1;	 
	const int			PATCH_SIZE		= 100;				// Size of the mesh grid in meters
	const int			LengthWave		= 60;	// Waves of wavelength between 60 / (512/2) = 0.23m (Nyquist limit) and 60 m

	inline static const char* SpectrumNames[] = {
						"Phillips",				// 0
						"Bretschneider",		// 1
						"Pierson-Moskowitz",	// 2
						"JONSWAP",				// 3
						"OchiHubble",			// 4
						"Texel-Marsen-Arsloe",	// 5
						"Donelan-Banner",		// 6
						"Torsethaugen",			// 7
						"Elfouhaily",			// 8
						"Horvath" };			// 9
	using SpectrumFunc = float (Ocean::*)(vec2);
	SpectrumFunc CurrentSpectrum = &Ocean::JONSWAP;
	void SetSpectrum(int spectre);

	// Parameters
	vec2				Wind			= { 0.0f, 1.0f };	// Input of wind (no need to be normalized at this stage)
	float				Amplitude		= 1.0f;				// Amplitude of the waves
	float				Lambda			= 0.5f;				// Factor of choppiness (exagerate the displacements)
	bool				bAutoLambda		= false;			// Adapt or not the choppiness
	int					Spectre			= 0;				// Type of spectrum (Philipps, JONSWAP, etc.)
	float				DirSpread		= 4.0f;
	float				Maturity		= 3.3f;				// 1 = no peak (full developped sea), 3.3 = value measured in the North Sea, 7 = young sea undergoing rapid development
	float				Fetch			= 50000.0f;			// In meters
	float				Depth			= 20.0f; 
	atomic<bool>		NeedsReinitFrequencies{ false };	// Thread-safe (no data race)
	atomic<bool>		NeedsClearRecords{ false };			// Thread-safe (no data race)

	vec3				OceanColor;
	int					iOceanColor		= 6;
	vector<vec3>		vOceanColors	= {
				vec3(67,  74,  55),	// 0 - Estuary
				vec3(30,  49,  42),	// 1 - North sea dark
				vec3(37,  66,  57),	// 2 - North sea medium
				vec3(22,  55,  60),	// 3 - Golf of Morbihan
				vec3( 1,  53,  75),	// 4 - Blue green
				vec3( 0,  59,  92),	// 5 - Light blue
				vec3( 0,  40,  55),	// 6 - Iroise
				vec3( 3,  92, 175),	// 7 - Pacific
				vec3( 0, 102, 133),	// 8 - Carribbean
				vec3( 1, 169, 193),	// 9 - Lagoon
				};

	float				PersistenceSec		= 1.0f;
	float				PersistenceFactor	= 1.0f;
	float				Transparency		= 0.05f;
	float				WhitecapCoverageReal = 0.0f;
	float				WhitecapCoverageTheoretical = 0.0f;

	vector<WaveData>	vWaveData;						// time, dx, dy, dz
	bool				bNewData			= false;
	float				HeightMax			= 0.0f;
	float				HeightMin			= 0.0f;

	bool				bVisible			= true;
	bool				bEnvMap				= true;
	bool				bShowPatches		= false;
	bool				bWireframe			= false;	

	vector<VkSemaphore>	ComputeFinishedSem;
	bool				ComputeWasPending	= false;

	bool				NeedsUpdateDescriptors = true;

	// Analysis
	vector<double>		a_Frequences;
	vector<double>		a_DensiteSpectrale;
	vector<double>		a_Directions;
	vector<double>		a_SpectreAccumule;

private:
	
	// Spectres
	void ComputeNormFactor();

	float Phillips(vec2 k);				// Phillips				1958  Base, simple, pas de fetch
	float PiersonMoskowitz(vec2 k);     // Pierson-Moskowitz	1964  Mer pleinement développée, pas de fetch
	float JONSWAP(vec2 k);              // Hasselmann et al.	1973  Fetch + pic de résonance ? (JONSWAP)
	float TexelMarsenArsloe(vec2 k);    // Kitaigorodskii		1983  JONSWAP + eau peu profonde (TMA)
	float DonelanBanner(vec2 k);        // Donelan-Banner		1985  Distribution directionnelle sech²
	float Elfouhaily(vec2 k);           // Elfouhaily et al.	1997  Gravité + capillaires, spectre unifié
	float Horvath(vec2 k);              // Horvath				2015  Variable, vieillissement, capillaires
	float Bretschneider(vec2 k);		// Bretschneider		1958  Mer pleinement développée, pas de fetch, spectre à deux pics
	float OchiHubble(vec2 k);			// Ochi-Hubble			1976  Mer pleinement développée, pas de fetch, spectre à deux pics
	float Torsethaugen(vec2 k);			// Torsethaugen			2004  Mer pleinement développée, pas de fetch, spectre à deux pics

	void CreateTextures();
	void CreateTexture2DArray();

	void CreateMesh();
	void CreateLODMesh(int meshSize, vector<sVertexOcean>& vertices, vector<unsigned int>& indices);
	void CreateLODMeshes();

	void CreateSpectrumPipeline();
	void CreateSpectrumDescriptors();
	void UpdateSpectrumDescriptors();
	
	void CreateTimeBuffer();

	void CreateIfftPipeline();
	void CreateIfftDescriptors();
	void UpdateIfftDescriptors(VulkanTexture& readTex, VulkanTexture& writeTex, int setIndex);
	void PrecomputeAllIfftDescriptors();

	void CreateDisplacementsPipeline();
	void CreateDisplacementsDescriptors();
	void UpdateDisplacementsDescriptors();

	void CreateGradientsPipeline();
	void CreateGradientsDescriptors();
	void UpdateGradientsDescriptors();

	// Get displacement
	void InitDisplacementsBuffer();
	void QueueAsyncReadback();
	void CheckReadbackCompletion();

	// Get foam
	void InitFoamBuffer();
	void QueueAsyncFoamReadback(uint32_t foamSlot);
	void CheckFoamReadbackCompletion();

	// Pipeline wireframe
	void CreatePipeline0();
	void CreateDescriptors0();
	void UpdateDescriptors0();

	// Pipeline 1 mesh
	void CreatePipeline1();
	void CreateDescriptors1();
	void UpdateDescriptors1();

	// Pipeline (LOD instancing)
	void CreatePipeline2();
	void CreateDescriptors2();
	void UpdateDescriptors2();
	void GetPatchesDecal(vec2 Position, float Yaw);
	void UpdateInstanceBuffer(Camera& camera, uint32_t frameIndex, bool bWithPatchdecal);
	
	// Ocean pipeline (LOD instancing with wake)
	void CreatePipeline3();
	void CreateDescriptors3();
	void UpdateDescriptors3();

	void CreateQueryPool();
	void GetTimestamps();

	void GetSpectrumStats(vector<float>& vS);

	// Miscellaneous
	float						mNormFactor					= 0.0000375f;
	float						mGravity					= 9.81f;

	shared_ptr<VulkanDevice>	mVulkanDevice;
	VkRenderPass				mRenderPass					= nullptr;
	VkExtent2D					mExtent;

	// Mesh
	unique_ptr<VulkanBuffer>    mVertexBuffer;
	unique_ptr<VulkanBuffer>    mIndexBuffer;
	uint32_t					mIndicesCount;
	VkSampler					mTextureSampler				= nullptr;

	// Textures
	VulkanTexture				mTexInitialSpectrum;
	VulkanTexture				mTexFrequencies;
	VulkanTexture				mTexUpdatedSpectra[2];
	VulkanTexture				mTexTempData;
	VulkanTexture				mTexDisplacements;
	VulkanTexture				mTexGradients;
	VulkanTexture				mTexFoamAcc[2];
	VulkanTexture			  * mTexFoamBuffer;
	VulkanTexture				mTexEnvmap;
	VulkanTexture				mTexFoamIntensity;
	VulkanTexture				mTexFoamBubbles;
	VulkanTexture				mTexFoamTexture;
	VulkanTexture				mTexWaterdUdV;

	// Uniform buffer pour time
	VkBuffer					mTimeBuffer					= nullptr;
	VkDeviceMemory				mTimeMemory					= nullptr;
	void					  * mTimeData;

	// Spectrum pipeline
	VkPipeline					mSpectrumPipeline			= nullptr;
	VkPipelineLayout			mSpectrumPipelineLayout		= nullptr;
	VkDescriptorSetLayout		mSpectrumDescriptorSetLayout= nullptr;
	VkDescriptorPool			mSpectrumDescriptorPool		= nullptr;
	VkDescriptorSet				mSpectrumDescriptorSet		= nullptr;

	// IFFT pipeline
	VkPipeline					mIfftPipeline				= nullptr;
	VkPipelineLayout			mIfftPipelineLayout			= nullptr;
	VkDescriptorSetLayout		mIfftDescriptorSetLayout	= nullptr;
	VkDescriptorPool			mIfftDescriptorPool			= nullptr;
	VkDescriptorImageInfo		mIfftImageInfo[8];
	VkDescriptorSet				mIfftDescriptorSets[4];		// Set 0: H -> Temp, Set 1: Temp -> V
	int							mCurrentSpectrum			= 0;

	// Displacement pipeline
	struct sDispPC
	{
		float Lambda;
		float Amplitude;
	};
	VkPipeline					mDisplacementsPipeline		= nullptr;
	VkPipelineLayout			mDisplacementsPipelineLayout = nullptr;
	VkDescriptorSetLayout		mDisplacementsDescSetLayout = nullptr;
	VkDescriptorPool			mDisplacementsDescPool		= nullptr;
	VkDescriptorSet				mDisplacementsDescSet		= nullptr;

	// Gradients pipeline
	VkPipeline					mGradientsPipeline			= nullptr;
	VkPipelineLayout			mGradientsPipelineLayout	= nullptr;
	VkDescriptorSetLayout		mGradientsDescriptorSetLayout= nullptr;
	VkDescriptorPool			mGradientsDescriptorPool	= nullptr;
	VkDescriptorSet				mGradientsDescriptorSets[2];
	bool						mFoamPingPong				= false;

	// Semaphores
	vector<VkFence>				mComputeFence;
	vector<VkCommandBuffer>		mComputeCmd;

	// Readback displacement
	unique_ptr<float[]>			mPixelsDisplacements		= nullptr;
	VkDeviceSize				mStagingSize;
	VkBuffer					mStagingBuffer;
	VkDeviceMemory				mStagingMem;
	void					  * mStagingData;
	VkFence						mReadbackFence;
	VkCommandBuffer				mReadbackCmd;
	bool						mReadbackPending			= false;

	// Readback foam
	unique_ptr<float[]>			mPixelsFoam					= nullptr;
	vector<float>				mvFoamHistory;

	VkDeviceSize				mFoamStagingSize;
	VkBuffer					mFoamStagingBuffer;
	VkDeviceMemory				mFoamStagingMem;
	void					  * mFoamStagingData;
	VkFence						mFoamReadbackFence;
	VkCommandBuffer				mFoamReadbackCmd;
	bool						mFoamReadbackPending		= false;

	// Wireframe rendering pipeline
	sPipeline_x					mWireframePipeline;

	// Ocean rendering pipeline (1 seul patch)
	sPipeline_x					mOneMeshPipeline;

	// Instances
	int							iMinPatchDecal = 0;
	int							iMaxPatchDecal = 0;
	int							jMinPatchDecal = 0;
	int							jMaxPatchDecal = 0;
	vector<sLODPatch>			mvLODPatches;
	vector<uint32_t>			mvMeshSizes = { 256, 128, 32, 8, 4 };	// LOD 0, 1, 2, 3, 4
	vector<sFrameData>			mFrames;

	// Ocean rendering pipeline (LOD instancié)
	sPipeline_x 					mLODPipeline;			// 5 LOD
	vector<sInstanceData>		mvInstanceData[5];

	// Ocean rendering pipeline (LOD instancing with wake)
	sPipeline_x 					mPipelineTexture;			// 1 LOD for the ocean around the ship
	vector<sInstanceData>		mvInstanceWakeData;

	// Timestapms
	VkQueryPool                 mQueryPool;
	chrono::high_resolution_clock::time_point mLastUpdateTime;
	uint64_t					mTimestampAccumulators[5] = { 0, 0, 0, 0, 0 };  // Sommes des ns
	uint64_t					mTimestampCounts[5] = { 0, 0, 0, 0, 0 };        // Nb d'échantillons
	bool						mFirstRun = true;

	// Kelvin wake
	VulkanTexture				mTexKelvinArray;
};

