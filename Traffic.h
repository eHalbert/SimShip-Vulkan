/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"
#include "vulkan_device.hpp"
#include "vulkan_ubo.hpp"
#include "Camera.h"
#include "Sky.h"
#include "Model.h"
#include "Sound.h"
#include "Light.h"
#include "HullMesh.h"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
// glm
#include <glm/glm.hpp>
// pugixml
#include "pugixml/pugixml.hpp"
#ifdef _DEBUG
#pragma comment(lib, "pugixml/Debug/pugixml.lib")
#else
#pragma comment(lib, "pugixml/Release/pugixml.lib")
#endif
using namespace glm;

// 3. WIN
#include <string>
#include <vector>
#include <map>

using namespace std;


extern SoundManager*    g_SoundMgr;
extern bool             g_bPause;
extern Camera           g_Camera;

enum class eNavigationalStatus : uint8_t 
{
    UnderWayUsingEngine                         = 0,
    AtAnchor                                    = 1,
    NotUnderCommand                             = 2,
    RestrictedManeuverability                   = 3,
    ConstrainedByDraught                        = 4,
    Moored                                      = 5,
    Aground                                     = 6,
    EngagedInFishing                            = 7,
    UnderWaySailing                             = 8,
    Reserved9                                   = 9,
    Reserved10                                  = 10,
    PowerDrivenTowingAstern                     = 11,
    PowerDrivenPushingAheadOrTowingAlongside    = 12,
    Reserved13                                  = 13,
    AIS_SART_Active                             = 14,
    Undefined                                   = 15
};
enum eShipType
{
    not_available = 0,
    // 1..19 reserved for future use
    wing_in_ground = 20,
    wing_in_ground_hazardous_cat_a = 21,
    wing_in_ground_hazardous_cat_b = 22,
    wing_in_ground_hazardous_cat_c = 23,
    wing_in_ground_hazardous_cat_d = 24,
    // 25..29 WIG reserved for future use
    fishing = 30,
    towing = 31,
    towing_large = 32, // exceeds 200m length or 25m breadth
    dredging_or_underwater_ops = 33,
    diving_ops = 34,
    military_ops = 35,
    sailing = 36,
    pleasure_craft = 37,
    // 38..39 reserved
    high_speed_craft = 40,
    high_speed_craft_hazardous_cat_a = 41,
    high_speed_craft_hazardous_cat_b = 42,
    high_speed_craft_hazardous_cat_c = 43,
    high_speed_craft_hazardous_cat_d = 44,
    // 45..48 HSC reserved for future use
    high_speed_craft_no_info = 49,
    pilot_vessel = 50,
    search_and_rescue_vessel = 51,
    tug = 52,
    port_tender = 53,
    anti_pollution_equipment = 54,
    law_enforcement = 55,
    // 56..57 spare, local vessel
    medical_transport = 58,
    noncombatant = 59,
    passenger = 60,
    passenger_hazardous_cat_a = 61,
    passenger_hazardous_cat_b = 62,
    passenger_hazardous_cat_c = 63,
    passenger_hazardous_cat_d = 64,
    // 65..68 Passenger reserved for future use
    passenger_no_info = 69,
    cargo = 70,
    cargo_hazardous_cat_a = 71,
    cargo_hazardous_cat_b = 72,
    cargo_hazardous_cat_c = 73,
    cargo_hazardous_cat_d = 74,
    // 75..78 Cargo reserved for future use
    cargo_no_info = 79,
    tanker = 80,
    tanker_hazardous_cat_a = 81,
    tanker_hazardous_cat_b = 82,
    tanker_hazardous_cat_c = 83,
    tanker_hazardous_cat_d = 84,
    // 85..88 Tanker reserved for future use
    tanker_no_info = 89,
    other = 90,
    other_hazardous_cat_a = 91,
    other_hazardous_cat_b = 92,
    other_hazardous_cat_c = 93,
    other_hazardous_cat_d = 94,
    // 95..98 Other type reserved for future use
    other_no_info = 99,
};

class Traffic
{
public:
    struct TrafficPoint
    {
        vec2    position;
        float   speed;
        bool    bArc = false;
    };
    vector<TrafficPoint>    vRoute;
    unique_ptr<Model>	    Ship;
    unique_ptr<LineMesh>    MeshSegments;
    unique_ptr<LineMesh>    MeshBeziers;

    vec2                    CurPos = vec2(0.0f);
    float                   CurYaw = 0.0f;
    bool                    bContour = true;
    bool                    bSound = true;

    Traffic(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const pugi::xml_node& node);
    ~Traffic();

    void Update(float dt);
    string NMEA_AIVDM_1();
    string NMEA_AIVDM_5();

    void RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

private:
    struct SegmentBezier
    {
        vec2 P0, P1, P2, P3;
        float speedStart;
        float speedEnd;
    };
    struct SegmentLine
    {
        vec2 start, end;
        float speedStart;
        float speedEnd;
    };
    enum class SegmentType { Line, Bezier };
    struct Segment
    {
        SegmentType type;
        union
        {
            SegmentLine line;
            SegmentBezier bezier;
        };
    };
    
    // Model 3D
    wstring             ModelName;
    //
	wstring             Name;
    wstring             CallSign;
	int                 IMO = 0;
	int				    ToBow = 0;          // In meters
	int				    ToStern = 0;        // In meters
	int				    ToPort = 0;         // In meters
	int				    ToStarboard = 0;    // In meters
	float			    Draught = 0.0f;     // In meters
	int				    EPFD = 0;           // 0=Undefined, 1=GPS, 2=GLONASS, 3=Combined, 4=Loran-C, 5=Chayka, 6=Integrated Navigation System, 7=Surveyed
    // Route
    vector<Segment>     mvSegments;
    float               mCurT = 0.0f;
    int                 mCurSegmentIndex = 0;
    vec2                mTangent;
    // Route VAO 
    GLuint		        mVaoSegments = 0;       // Segments
    int			        mIndicesSegments = 0;
    GLuint		        mVaoBezier = 0;         // Bézier curves
    int			        mIndicesBezier = 0;
    // Lights
    GLuint              mQuadVAO = 0;
    vector<vec3>        mvLightPositions;
    vector<vec3>        mvLightColors;

    unique_ptr<Sound>	mSoundThrust;
	float			    mSpeedMaxKt = 10.0f;

    wstring             MMSI;
	float               SOG;        // Speed over ground in knots
	float 			    COG;        // Course over ground in degrees
	float               Longitude;
	float               Latitude;
	float			    ROT;        // Rate of turn      
    float               PrevYaw;
    eShipType           ShipType;

    unique_ptr<Light>   mLight;

    void RenderOneLight(VkCommandBuffer cmd, Camera& camera, int i);
    
    eShipType stringToShipType(const std::string& typeStr);
    inline uint8_t encodeAISChar6(char c);
    unsigned char calculate_checksum(const string& sentence);
    string encodeAISPayloadToNMEAASCII(const string& bin);

    vec2 EvaluateBezier(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, float t);
    vec2 EvaluateBezierDerivative(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, float t);
    float ApproximateBezierLength(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, int steps = 20);
    void ConstructSegmentsFromRoute();
    void BuildSegmentsVAO(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
    void BuildBezierVAO(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
    void UpdateSound();
};

class Traffics 
{
public:
    vector<Traffic*>vTraffics;
    bool			bLights = true;
    bool            bVisible = true;
	bool			bShowRoute = false; 

    Traffics(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const string& filename);
    ~Traffics();

    bool LoadFromFile(const string& filename);
    float smooth_dt(float newVal);
    void Update(uint32_t currentImage, Camera& camera, Sky* sky);
    string NMEA_AIVDM_1();
    string NMEA_AIVDM_5(int index);

    void RenderOpaque(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderTransparent(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights = false);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    unique_ptr<VulkanSwapChain> mSwapChain;
    VkRenderPass				mRenderPass;
    VkExtent2D					mExtent;

};