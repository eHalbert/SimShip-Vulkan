/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Structures.h"
#include "Utility.h"
#include "Ocean.h"
#include "Camera.h"
#include "Model.h"
#include "Sound.h"
#include "Timer.h"
#include "HullMesh.h"
#include "Smoke.h"
#include "Spray.h"
#include "Flag.h"
#include "Light.h"

// 2. LIB
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
// glm
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
using namespace glm;
// libigl
#include <igl/readOBJ.h>
// Eigen
#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Sparse>

// 3. WIN
#define NOMINMAX
#include <windows.h>
#include <stdlib.h>
#include <cstdio>
#include <iostream>
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <thread>
#include <variant>
#include <random>

using namespace std;


struct sTriangle
{
	int			I[3];								// Indices of the face
	int			bUnder[3];							// Status relative to water height
	int			WaterStatus;						// 0 = under, 1 or 2 vertex under, 3 = above
	vec3		Color	= vec3(0.0f, 0.0f, 0.0f);	// Color of the triangle in debug mode
	float		Area	= 0.0f;						// Total area
	vec3		CoG		= vec3(0.0f, 0.0f, 0.0f);	// Centre of gravity
	vec3		NormalInitial = vec3(0.0f, 0.0f, 0.0f);	// Inital normal vector
	vec3		Normal	= vec3(0.0f, 0.0f, 0.0f);	// Normal vector

	float		Depth;								// Depth
	vec3		vPressure;							// Pressure force vector
	float		fPressure;							// Pressure force magnitude
};
struct sSegment
{
	vec3 a, b;
};
struct sForce
{
	float		Magnitude	= 0.0f;
	vec3		Vector		= vec3(0.0f);
	vec3		Position	= vec3(0.0f);
	string		Name		= "";
	int			Decimal		= 0;
	string		Unit		= "N";
};
struct sSprayPt { vec3 p; vec3 n; };
template<typename T>
struct SmoothFilter
{
	int   nbValues;
	int   nbIgnored;
	std::vector<T> values;
	int   index = 0;
	bool  filled = false;
	int   callCount = 0;
	T     sum;

	SmoothFilter(int nbValues, int nbIgnored, T zero)
		: nbValues(nbValues), nbIgnored(nbIgnored),
		values(nbValues, zero), sum(zero)
	{}

	T update(T newVal)
	{
		callCount++;
		if (callCount <= nbIgnored)
			return newVal;

		if (filled)
			sum -= values[index];

		sum += newVal;
		values[index] = newVal;

		index = (index + 1) % nbValues;
		if (index == 0) filled = true;

		int count = filled ? nbValues : index;
		return sum / static_cast<float>(count);
	}
};
class Ship
{
public:
	Ship() {};
	Ship(shared_ptr<VulkanDevice>& vulkanDevice,
		VkRenderPass renderPassScene, VkExtent2D extent,
		VkRenderPass renderPassReflection,
		VkRenderPass randerPassShadow, uint32_t shadowWidth, uint32_t shadowHeight,
		VkRenderPass randerPassBridgeMask,
		VkRenderPass randerPassWake, sShip& ship, Ocean* ocean, Camera& camera);
	~Ship();

	void	SetOcean(Ocean* ocean);
	void	SetMass() { mMass = ship.Mass_t * 1000.0f; }
	float	GetLength() { return mLength; }
	float	GetWidth() { return mWidth; }
	sBBox	GetBoundingBox() { return mBbox; }
	mat4	GetWorld() { return mWorld; }
	void	ResetVelocities();
	vec3	TransformPosition(vec3 v);
	vec3	TransformVector(vec3 v);
	void	SetYawFromHDG(float hdg);
	void	CreateKelvinImages();
	void	Update(float time);
	string	NMEA_RMC();
	string	NMEA_VHW();
	string	NMEA_VWR();

	void	UpdateShadowUBOs(uint32_t imageIndex, Camera& camera, Sky* sky);
	void	RenderShadow(VkCommandBuffer cmd, int iCurrentFrame);

	void	UpdateReflectionUBOs(uint32_t imageIndex, Camera& camera, Sky* sky);
	void	RenderReflection(VkCommandBuffer cmd, int iCurrentFrame);

	void	UpdateWakeUBO(uint32_t imageIndex, int sizeW, int sizeH);
	void	RenderWake(VkCommandBuffer cmd, int iCurrentFrame, int imageIndex);

	void	UpdateBridgeMaskUBO(uint32_t imageIndex, Camera& camera);
	void	RenderOpaqueWalls(VkCommandBuffer cmd, int iCurrentFrame);
	void	RenderWindows(VkCommandBuffer cmd, int iCurrentFrame);

	void	UpdateUBO(VkCommandBuffer cmd,uint32_t imageIndex, Camera& camera, Sky* sky);
	void	RenderTransparent(VkCommandBuffer cmd, int iCurrentFrame);
	void	RenderOpaque(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky);

	void	RenderSpray(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky);
	void	RenderSmoke(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky);

	void	RenderNavLights(VkCommandBuffer cmd, Camera& camera);

	void	RecreatePipelines(VkRenderPass renderPassScene, VkRenderPass renderPassReflection, VkRenderPass renderPassShadow, VkRenderPass randerPassBridgeMask, VkRenderPass randerPassWake, VkExtent2D swapChainExtent);

	eh::Timer			chrono;
	sShip				ship;

	string				InfoModel;
	string				Info3D;

	// Motion
	float				AreaWetted			= 0.0f;
	float				LWL					= 0.0f;
	float				Yaw					= 0.0f;
	float				Pitch				= 0.0f;
	float				Roll				= 0.0f;
	float				HDG					= 0.0f;
	float				SOG					= 0.0f;
	float				COG					= 0.0f;
	float				SOGbow				= 0.0f;
	float				SOGstern			= 0.0f;
	vec2				vCOG				= vec2(0.0f);

	float				PitchCouple			= 0.0f;
	float				RollCouple			= 0.0f;
	float				PitchAcceleration	= 0.0f;
	float				RollAcceleration	= 0.0f;
	float				PitchVelocity		= 0.0f;
	float				RollVelocity		= 0.0f;
	float				HeaveAcceleration	= 0.0f;
	float				HeaveVelocity		= 0.0f;
	float				SurgeAcceleration	= 0.0f;
	float				SurgeVelocity		= 0.0f;
	float				YawAcceleration		= 0.0f;
	float				YawVelocity			= 0.0f;
	float				SwayVelocity		= 0.0f;	
	float				WindAcceleration	= 0.0f;
	float				WindVelocity		= 0.0f;
	float				LinearVelocity		= 0.0f;
	float				DriftVelocity		= 0.0f;
	float				DriftAngleDeg		= 0.0f;
	float				TurnDiameter_m		= 0.0f;
	float				TurnDiameter_L		= 0.0f;
	float				AWS					= 0.0f;
	float				AWD					= 0.0f;
	float				AWA					= 0.0f;
	char				WindLeftRight;

	// Engine
	int					PowerCurrentStep1	= 0;
	float				PowerApplied1		= 0.0f;		// kW
	float				PropRpm1			= 0.0f;
	int					PowerCurrentStep2	= 0;
	float				PowerApplied2		= 0.0f;		// kW
	float				PropRpm2			= 0.0f;

	// Rudder
	int					RudderCurrentStep	= 0;
	float				RudderAngleDeg		= 0.0f;		// Deg

	// Bow Thruster 1
	int					BowThrusterCurrentStep = 0;
	float				BowThrusterApplied	= 0.0f;		// kW
	float				BowThrusterRpm		= 0.0f;

	// Bow Thruster 2
	int					SternThrusterCurrentStep = 0;
	float				SternThrusterApplied = 0.0f;	// kW
	float				SternThrusterRpm	= 0.0f;

	// Autopilot
	bool				bAutopilot			= false;
	int					HDGInstruction		= 0;
	float				RudderTargetDeg		= 0.0f;

	// Switches
	bool				bVisible			= true;
	bool				bMotion				= true;
	bool				bModel				= true;
	bool				bHullMesh			= false;
	bool				bWireframe			= false;
	bool				bOutline			= false;
	bool				bAxis				= false;
	bool				bForces				= false;
	bool				bPressure			= false;
	int					WaterSearch			= 0;
	bool				bSound				= true;
	bool				bLights				= false;
	bool				bSmoke				= true;
	bool				bSpray				= true;
	bool				bRadar				= true;
	bool				bFlag				= true;
	bool				bKelvinWakes		= true;
	bool				bWakeMesh			= false;
	bool				bContour			= false;
	bool				bBbox				= false;

	vector<sResultData>	vForcesData;

private:
	void	InitDimensions();
	void	InitTriangles();
	void	InitCentroid();
	void	InitSurfaces();
	void	InitInertia();
	void	InitWaterVertices();
	void	InitHullMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent);
	void	InitPressureMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent);
	void	InitContours(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent);
	void	InitModels(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent,
		VkRenderPass renderPassReflection, VkRenderPass randerPassShadow, uint32_t shadowWidth, uint32_t shadowHeight, VkRenderPass randerPassBridgeMask);
	void	InitWakeMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent, VkRenderPass randerPassWake);
	void	InitSounds(Camera& camera);
	void	InitSpray(vector<vec3>& contour);

	// Contour
	vector<vec3> ComputeContour();
	vector<vec3> ArrangeContour(const vector<vec3>& contourUnordered);
	vector<vec3> ArrangeByCoordinates(const vector<vec3>& contourUnordered);
	vector<vec3> ArrangeByPolarAngle(const vector<vec3>& contourUnordered);
	vector<vec3> ArrangeByNearestNeighbor(const vector<vec3>& contourUnordered);
	
	vector<vec3> OffsetContour(const vector<vec3>& contour, float offset);
	void	CreateTextureOfContour(shared_ptr<VulkanDevice>& vulkanDevice, const vector<vec3>& contour);

	vec3	GetVerticeAtMeshIndex(int x, int z);
	int		GetHeightFast(vec3& pos);
	int		GetHeightSlow(vec3& pos);
	void	UpdateWorldMatrix();
	void	TransformVertices();
	void	GetHeightOfAllVertices();
	void	GetTrisUnderWater();
	void	ComputeMaxSpeed();
	void	ComputeEquilibriumDraft();

	// Wake by vao
	void	UpdateWakeMesh();

	// SYSTEM OF FORCES
	void	ComputeArchimede();
	void    ComputeGravity();
	void	ComputeHeaveDrag();
	void	ComputeMainThrust(float dt);
	void	ComputePropellersDrag();
	void	ComputeViscousDrag();
	void	ComputeWavesDrag();
	void	ComputeBowThrust(float dt);
	void	ComputeSternThrust(float dt);
	void	ComputeRudder(float dt);
	void	ComputeWind();
	void	ComputeCentrifugal();
	float	ComputePivotPosition();
	void	ComputeForces(float dt);
	void	ComputeTurningCircle(float dt);
	void	ComputeAutopilot(float dt);
	void	UpdatePressureMesh();

	void	UpdateSounds();

	void	UpdateSmoke(VkCommandBuffer cmd, int iCurrentFrame);

	void	UpdateSpray(int iCurrentFrame);

	void	UpdateFlag(int iCurrentFrame);
	void	RenderFlag(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky);

	void	RenderOneLight(VkCommandBuffer cmd, Camera& camera, int i);

	shared_ptr<VulkanDevice>	mVulkanDevice;

	// Hull for physics
	string				mPathnameHull;
	Eigen::MatrixXd		mV;
	Eigen::MatrixXi		mF;
	vector<vec3>		mvVertices;
	vector<vec3>		mvVerticesInitial;
	vector<float>		mvVertexColored;
	vector<int>			mvVertSubmerged;
	vector<float>		mvVertWaterHeight;
	vector<sTriangle>	mvTris;
	unique_ptr<HullMesh>mHullMesh;
	unique_ptr<LineMesh>mContourMesh1;
	unique_ptr<LineMesh>mContourMesh2;
	unique_ptr<LineMesh>mPressureMesh;

	// Full model
	string				mPathnameFull;
	sBBox				mBbox;					// Bounding box

	// Forces
	sForce				mArchimede;
	sForce				mGravity;
	sForce				mHeaveDrag;
	sForce				mThrust1;
	sForce				mThrust2;
	sForce				mPropDrag1;
	sForce				mPropDrag2;
	sForce				mViscousDrag;
	sForce				mWavesDrag;
	sForce				mBowThrust;
	sForce				mSternThrust;
	sForce				mRudderLift;
	sForce				mRudderDrag;
	sForce				mAirDrag;
	sForce				mWindTorque;
	sForce				mWindDrift;
	sForce				mCentrifugalTorque;

	// Models
	unique_ptr<Model>	mModelFull			= 0;
	unique_ptr<Model>	mPropeller1			= 0;	// Left propeller
	unique_ptr<Model>	mPropeller2			= 0;	// Right propeller
	unique_ptr<Model>	mRudder1			= 0;
	unique_ptr<Model>	mRudder2			= 0;
	unique_ptr<Model>	mRadar1				= 0;
	unique_ptr<Model>	mRadar2				= 0;
	unique_ptr<Model>	mAxis				= 0;

	// Ocean
	Ocean			  * mOcean				= nullptr;	// Reference to the ocean object
	float			  * pDisplacement		= nullptr;	// Map of the displacement of the vertices of the ocean mesh
	vector<vector<vec3>>mvWaterPos;

	// Physical characteristics
	float				mMass				= 1.0f;			// kg
	float				mPowerW				= 1.0f;			// watt
	float				mLength				= 0.0f;			// m
	float				mLength3			= 0.0f;			// m3
	float				mWidth				= 0.0f;			// m
	float				mHeight				= 0.0f;			// m
	float				mVolume				= 0.0f;			// m3
	float				mDraft				= 0.0f;			// m
	float				mAirDraft			= 0.0f;			// m
	float				mAreaWettedMax		= 0.0f;			// m2
	float				mAreaXZ				= 0.0f;			// m2
	float				mAreaXZ_RacCub		= 0.0f;
	float				mAreaPropeller		= 0.0f;

	vec3				mCentroid			= vec3(0.0f);
	float				mIxx				= 0.0f;
	float				mIyy				= 0.0f;
	float				mIzz				= 0.0f;
	float				mIxy				= 0.0f;
	float				mIxz				= 0.0f;
	float				mIyz				= 0.0f;

	vec3				mBow				= vec3(0.0f);	// From centre to bow
	vec3				mStern				= vec3(0.0f);	// From centre to stern
	vec3				mWakePivot			= vec3(0.0f);	// Close to the stern;
	float				mRudderArea			= 0.0f;
	float				mCosPhi				= 0.0f;
	float				mSinPhi				= 0.0f;
	float				mTurnYawAccum		= 0.0f;
	float 				mTurnEntrySpeed		= 0.0f;
	float 				mPrevSurgeVelocity	= 0.0f;
	float 				mSpeedAtPhase2		= 0.0f;
	float 				mYawAtPhase2		= 0.0f;
	bool  				mTurnPhase2			= false;
	float				BowThrusterYawVelocity = 0.0f;
	float 				SternThrusterYawVelocity = 0.0f;
	float				YawAccBowThruster = 0.0f;
	float 				YawAccSternThruster = 0.0f;
	float				mBowThrustMax		= 0.0f;
	float				mSternThrustMax		= 0.0f;
	float				mCosYaw				= 0.0f;
	float				mSinYaw				= 0.0f;

#ifdef _DEBUG
	// Turning circle metrics
	bool 				mTurnArmed			= false;
	bool  				mTurnStarted		= false;
	float 				mTurnStartSpeed		= 0.0f;
	float 				mTurnElapsedTime	= 0.0f;
	float 				mPrevRudderAngle	= 0.0f;
	vec2  				mTurnStartPos		= vec2(0.0f);
	float 				mTurnStartHDG		= 0.0f;
	float 				mTurnHdgAccum		= 0.0f;
	float 				mPrevHDG			= 0.0f;
	int   				mTurnStopFrames		= 0;
	int   				mTurnMetricsDone	= 0;
	bool  				mTC_090_Printed		= false;
	bool  				mTC_180_Printed		= false;
	bool  				mTC_270_Printed		= false;
	bool  				mTC_360_Printed		= false;
	float 				mTC_090_Speed		= 0.0f;
	float 				mTC_090_Advance		= 0.0f;
	float 				mTC_090_Transfer	= 0.0f;
	float 				mTC_180_Speed		= 0.0f;	
	float 				mTC_180_TactDiam	= 0.0f;
	float 				mTC_270_Speed		= 0.0f;
	float 				mTC_360_Speed		= 0.0f;
	float 				mTC_360_TurnDiam	= 0.0f;
#endif

	SmoothFilter<float> mSmoothDt{ 500, 300, 0.0f };
	SmoothFilter<vec2>  mSmoothDpos{ 500, 100, vec2(0.0f) };
	SmoothFilter<float> mSmoothSogBow{ 500, 300, 0.0f };
	SmoothFilter<float> mSmoothSogStern{ 500, 300, 0.0f };

	// World matrice and time
	mat4				mWorld				= mat4(1.0f);
	float               mDt					= 0.0f;			// Elapsed time since last frame

	// Constants
	const float			mGRAVITY				= 9.80665f;	// m / s^2
	const float			mWATER_DENSITY			= 1025.f;	// SI = kg / m3
	const float			mKINEMATIC_VISCOSITY	= 1.19e-6f;	// Viscosité cinématique de l'eau de mer (m^2/s)
	const float			mAIR_DENSITY			= 1.225f;	// SI = kg / m3
	const float			mPLATE_DRAG_COEFF		= 1.28f;

	unique_ptr<Sound>	mSoundThrust1;
	unique_ptr<Sound>	mSoundThrust2;
	unique_ptr<Sound>	mSoundBowThruster;
	unique_ptr<Sound>	mSoundSternThruster;
	bool				mbSoundBowThrusterPlaying = false;
	bool				mbSoundSternThrusterPlaying = false;

	// Trail (wake) (Projection of VAO on texture)
	vector<sFoamPts>	vWakePoints;				// Points taken every second to mark the wake
	vector<sFoamVertex>	vWakeVertices;				// From the points create a vao with vertices making triangles
	unique_ptr<WakeMesh>mWakeMesh;
	vector<vec3>		mWakeSideLeft;
	vector<vec3>		mWakeSideRight;

	// Smoke
	unique_ptr<Smoke>	mSmoke;

	// Spray
	vector<sSprayPt>	mLeft;
	vector<sSprayPt>	mRight;
	float				mRandomOffsetRange = 0.1f;
	unique_ptr<Spray>	mSpray;
	mt19937                             mRng{ random_device{}() };
	uniform_real_distribution<float>	mDist{ -1.0f, 1.0f };

	// Flag
	unique_ptr<Flag>	mFlag;

	// Light
	unique_ptr<Light>	mLight;
};

