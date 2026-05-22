/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "vulkan_ubo.hpp"
#include "vulkan_buffer.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#include <memory>
using namespace std;

struct WaveData
{
    double          time;
    double          dx, dy, dz;
};
struct sMatrixUBO
{
    mat4            model;
    mat4            view;
    mat4            proj;
};
struct sMatrixShadowUBO
{
    mat4            model;
    mat4            view;
    mat4            proj;
    mat4            lightViewProj;
};
struct sMatrixReflUBO
{
    mat4            model;
    mat4            view;
    mat4            proj;
    vec4            clipPlane;
};
struct sShadowUBO
{
    mat4            lightSpaceMatrix;
    mat4            model;
};
struct sMatrixColorUBO
{
    mat4            model;
    mat4            view;
    mat4            proj;
    vec4            color;
};
struct sViewUBO
{
    vec3            position;
    float           pad;
};
struct sOceanUBO
{
    mat4            matViewProj;
    
    vec3            eyePos;
    int             bEnvmap;
    
    mat4            lightSpaceMatrix;
    
    vec3            oceanColor;
    float           transparency;
    
    vec3            sunColor;
    float           time;
    
    vec3            sunDir;
    float           exposure;
    
    vec3            shipPosition;   // Ship position (world)
    float           shipRotation;   // Ship heading (radians, 0 = X)
    
    int             bKelvinWakes;
    float           amplitude;      // Height of the waves
    float           kelvinScale;
    float           centerFore;
    
    int             bShowPatches;
    int             bShowShadow;
    int             bShowReflection;
    int             bShowWake;
    
    vec2            shipSize;
    vec2	        shipPivot;
   
    vec2            wakeSize;
	int			    texLayer;
    float 		    mistDensity;

    vec2    windDir;        // Wind direction vector in world XZ (e.g. (-7.7, 0) = westerly)
    float   windRippleStr;  // Capillary ripple strength [0..1], 0 = disabled
    float   windSpeed;      // Ripple tile scale (try 0.05..0.3)

};
struct sPipeline_1   // For 1 frame in flight
{
    VkPipeline              pipeline        = nullptr;
    VkPipelineLayout        pipelineLayout  = nullptr;
    VkDescriptorSetLayout   descSetLayout   = nullptr;
    VkDescriptorPool        descPool        = nullptr;
    VkDescriptorSet         descSet         = nullptr;
    unique_ptr<VulkanUBO>   ubo;

    void destroy(VkDevice device)
    {
        if (descPool != VK_NULL_HANDLE) 
        {
            vkDestroyDescriptorPool(device, descPool, nullptr);
            descPool = VK_NULL_HANDLE;
        }
        descSet = VK_NULL_HANDLE;
        if (descSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
            descSetLayout = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) 
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (pipeline != VK_NULL_HANDLE) 
        {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
		ubo.reset();
        ubo = VK_NULL_HANDLE;
    }
};
struct sPipeline_x   // For x frames in flight
{
    VkPipeline                      pipeline        = nullptr;
    VkPipelineLayout                pipelineLayout  = nullptr;
    VkDescriptorSetLayout           descSetLayout   = nullptr;
    VkDescriptorPool                descPool        = nullptr;
    vector<VkDescriptorSet>         descSet;
    vector<unique_ptr<VulkanUBO>>   ubo;

    void destroy(VkDevice device)
    {
        if (descPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, descPool, nullptr);
            descPool = VK_NULL_HANDLE;
        }
        for (auto& ds : descSet)
            ds = VK_NULL_HANDLE;
        descSet.clear();
        if (descSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
            descSetLayout = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        for (auto& u : ubo)
            u.reset();
        ubo.clear();
    }
};
struct sPipeline_x_2   // For x frames in flight with 2 pipelines (opaque + transparent)
{
    VkPipeline              pipelineOpaque      = nullptr;
    VkPipeline              pipelineTransparent = nullptr;
    VkPipelineLayout        pipelineLayout      = nullptr;
    VkDescriptorSetLayout   descSetLayout       = nullptr;
    VkDescriptorPool        descPool            = nullptr;
    vector<VkDescriptorSet> descSet;

    void destroy(VkDevice device)
    {
        if (descPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, descPool, nullptr);
            descPool = VK_NULL_HANDLE;
        }
        for (auto& ds : descSet)
            ds = VK_NULL_HANDLE;
        descSet.clear();
        if (descSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
            descSetLayout = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (pipelineOpaque != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipelineOpaque, nullptr);
            pipelineOpaque = VK_NULL_HANDLE;
        }
        if (pipelineTransparent != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipelineTransparent, nullptr);
            pipelineTransparent = VK_NULL_HANDLE;
        }
    }
};

extern class Model;
struct sTerrain
{
    string	file;
    string  name;
    float	xMin = 0.0f;
    float	xMax = 0.0f;
    float	zMin = 0.0f;
    float	zMax = 0.0f;
    vec2	center = vec2(0.0f);
    float	widthMeters = 0.0f;
    float	heightMeters = 0.0f;
    int		zoom = 14;
    unique_ptr<Model> model;
    vec3	pos = vec3(0.0f);
    vec3	scale = vec3(1.0f);

    sTerrain() noexcept = default;
};
struct sPositions
{
    string	name;
    vec2	pos;
    float	heading; // in degrees
};
struct sLine
{
    vec2 p1;
    vec2 p2;
};
struct sBBox
{
    vec3 min;
    vec3 max;
};
enum eClass { FastBoat = 0, Corvette, Frigate, Fishing, Submarine, Ferry, Tugboat, Cargo, Supertanker };

struct sShip
{
    // Files
    string		ShortName                   = "";			// Name
    string		PathnameHull                = "";			// Pathname of the hull for simulation
    string		PathnameFull                = "";			// Pathname of the model for rendering
    string		PathnamePropeller1          = "";			// Pathname of the propeller (moving piece)
    string		PathnamePropeller2          = "";			// Pathname of the propeller (moving piece)
    string		PathnameRudder              = "";			// Pathname of the rudder (moving piece)
    string		PathnameRadar1              = "";			// Pathname of the radar (moving piece)
    string		PathnameRadar2              = "";			// Pathname of the radar (moving piece)
    string		PathnameFlag                = "";			// Pathname of the flag
    string		ThrustSound                 = "";			// Name of the sound file
    string		BowThrusterSound            = "";			// Name of the sound file
    string		SternThrusterSound          = "";			// Name of the sound file

    // Positions
    vec3		Position                    = vec3(0.0f);	// Position of the ship (at the center)
    vec3		Rotation                    = vec3(0.0f);	// Rotation on the 3 axis
    vec3		ViewWheel                   = vec3(0.0f);	// View on the bridge - center
    vec3		ViewLeft                    = vec3(0.0f);	// View on the bridge - left
    vec3		ViewRight                   = vec3(0.0f);	// View on the bridge - right
    vec3		ViewBow                     = vec3(0.0f);	// View at the bow
    vec3		ViewStern                   = vec3(0.0f);	// View at the stern

    // Dimensions
    eClass		Class                       = eClass::Corvette;	// Class of ship that defines certain parameters
    float		Length                      = 10.0f;		// Final length
    float		SpeedMaxKt                  = 15.0f;		// Speed max after sea trials
    float		SpeedEcoKt                  = 14.0f;		// Speed for sea trials at notch 7/10
    float		Mass_t                      = 1.0f;			// Tons
    vec3		PosGravity                  = vec3(0.0f);	// Offset of the center of gravity (relative to Position)
    float		PitchRollFactor             = 1.0f;         // 1.0 = natural, 0.5 = half speed, 2.0 = double speed
    float		EnvMapFactor                = 0.0f;			// Factor of environment reflexion (between 0.0 and 1.0)
    int			ContourType                 = 1;			// 1 (by coordinates) or 2 (by polar angle)
    float       AreaWetted                  = 0.0f;    
	float	    LWL                         = 0.0f;         // Length of Water Line
    float       PositionY                   = 0.0f;         // Position of the ship after computation of Archimede/Gravity
    float		AreaFront                   = 0.0f;
    vec3		AreaFrontCenter             = vec3(0.0f);
    float		AreaLat                     = 0.0f;
    vec3		AreaLatCenter               = vec3(0.0f);

    // Power
    vec3		PosPower                    = vec3(0.0f);	// Offset of the center of the propeller where the power is applied (relative to Position)
    float		PowerkW                     = 1000.0f;		// kiloWatts
    int			PowerStepMax                = 10;			// Number of steps on the throttle lever

    // Propellers
    float		PropRpmMax                  = 200.0f;		// Maximum RPM of the propeller
    float		PropRpmIncrement            = 20.0f;		// Rate of increase/decrease RPM of the propeller
    int			nPropeller                  = 2;			// Number of propellers
    vec3		PosPropeller1               = vec3(0.0f);	// Left propeller
    float		PropTorque1                 = 0.0f;			// +1.0 for right propeller, -1.0 otherwise
    vec3		PosPropeller2               = vec3(0.0f);	// Right propeller
    float		PropTorque2                 = 0.0f;			// +1.0 for right propeller, -1.0 otherwise
    float		PropDiameter                = 3.0f;			// Diameter of the propeller
    float		WakeWidth                   = 1.0f;			// Real wake width is mWidth x WakeWidth

    // Rudder
    vec3		PosRudder                   = vec3(0.0f);	// Offset of the center of the rudder (relative to Position)
    int			RudderIncrement             = 1;			// Degrees
    int			RudderStepMax               = 35;			// Number of increments
    float		RudderRotSpeed              = 10.0;			// Degrees / sec
    int			nRudder                     = 2;			// Number of rudders
    vec3		PosRudder1                  = vec3(0.0);	// Left
    vec3		PosRudder2                  = vec3(0.0);	// Right

    // Turning
    float		RoTMax                      = 120.0f;		// Maximum rate of turn (°/min)
    float		TurnabilityAtSpeed          = 0.1f;			// Coefficient applied to the rate of turn at SpeedEcoKt
    float		PivotFwd                    = 0.2f;			// Pivot point in forward motion (from Bow to length (stern))
    float		PivotBwd                    = 0.7f;			// Pivot point in backward motion (from Bow to length (stern))
    float		CentrifugalPerf             = 10.0f;		// Performance of the centrifugal force
    
    // Bow Thruster
    bool		HasBowThruster              = true;
    vec3		PosBowThruster              = vec3(0.0);	// Offset of the center of the center of the bow thruster (relative to Position)
    float		BowThrusterPerf             = 0.4f;			// Performance of efficiency of the system Engine - Propeller
    float		BowThrusterPowerW           = 10000.0;		// Watts
    int			BowThrusterStepMax          = 5;			// Number of steps on the throttle lever
    float		BowThrusterRpmMin           = 0.0f;			// Minimum RPM of the propeller
    float		BowThrusterRpmMax           = 500.0f;		// Maximum RPM of the propeller
    float		BowThrusterRpmIncrement     = 10.0f;		// Rate of increase/decrease RPM of the propeller

    // Stern Thruster
    bool		HasSternThruster            = true;
    vec3		PosSternThruster            = vec3(0.0);	// Offset of the center of the center of the stern thruster (relative to Position)
    float		SternThrusterPerf           = 0.4f;			// Performance of efficiency of the system Engine - Propeller
    float		SternThrusterPowerW         = 10000.0f;		// Watts
    int			SternThrusterStepMax        = 5;			// Number of steps on the throttle lever
    float		SternThrusterRpmMin         = 0.0f;			// Minimum RPM of the propeller
    float		SternThrusterRpmMax         = 500.0f;		// Maximum RPM of the propeller
    float		SternThrusterRpmIncrement   = 10.0f;	    // Rate of increase/decrease RPM of the propeller

    // Autopilot
    float		BaseP                       = 4.0f;			// Make the turn
    float		BaseI                       = 2.0f;			// Correct a constant deviation
    float		BaseD                       = 4.0f;			// Anticipate the end of the turn
    float		MaxIntegral                 = 5.0f;			// Limit of the integral to avoid runaway

    // Radar
    int			nRadar                      = 0;
    vec3		PosRadar1                   = vec3(0.0f);
    float		RotationRadar1              = 40.0f;		// RPM
    vec3		PosRadar2                   = vec3(0.0f);
    float		RotationRadar2              = 40.0f;		// RPM

    // Flag
    bool		bFlag                       = true;
    vec3		PosFlag                     = vec3(0.0f);
    float		DimXFlag                    = 1.0f;

    // Spray
    float		SprayVerticalPerf           = 10.0f;		// Vertical spray performance
    int			SprayMultiplier             = 1;			// Number of points between points of contour
    float		SprayLength                 = 0.1f;			// % of length of the ship taken on the contour
    int			SprayType                   = 0;			// 0 = sharp (like a frigate), 1 = rounded (like a cargo)

    // Chimneys
    int			nChimney                    = 2;			// Number of chimneys
    vec3		PosChimney1                 = vec3(0.0f);	// Left chimney
    vec3		PosChimney2                 = vec3(0.0f);	// Right chimney

    // Lights
    vector<vec3>LightPositions;						        // Offset of the center of the lights (relative to Position)
    vector<vec3>LightColors;						        // Color the lights
    
    // Waves
    float		CenterFore                  = 0.0f;			// Reference X for the position of the kelvin texture
    int			BaseFroude                  = 0;			// Normally = 0, meaning that the real froude scheme is take, otherwise, take a higher Froude scheme
};
struct sResultData
{
    string	variable;
    double	value;
    int		decimal;
    string	unit;
};
struct sFoamPts
{
    vec3		pos;		// Series of points for the wake	
    float		time;		// Time of creation (for alpha fading)
};
struct sFoamVertex
{
    vec3		pos;		// Position 3D du sommet
    vec2		uv;			// Coordonnée dans la texture
    float		alpha;
};
