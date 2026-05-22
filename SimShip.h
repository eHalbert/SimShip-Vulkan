/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

// 1. PROJET
#include "Utility.h"
#include "Timer.h"
#include "Camera.h"
#include "Spectra.h"
#include "Ocean.h"
#include "Model.h"
#include "Sky.h"
#include "Clouds.h"
#include "ScreenQuad.h"
#include "vulkan_debug.hpp"
#include "vulkan_buffer.hpp"
#include "vulkan_swapchain.hpp"
#include "Sound.h"
#include "Ship.h"
#include "Ini.h"
#include "Lighthouses.h"
#include "Markup.h"
#include "UdpSender.h"
#include "Traffic.h"
#include "Overlay.h"

// 2. LIB

// glfw
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
// imgui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
// pugixml
#include "pugixml/pugixml.hpp"
#ifdef _DEBUG
#pragma comment(lib, "pugixml/Debug/pugixml.lib")
#else
#pragma comment(lib, "pugixml/Release/pugixml.lib")
#endif

// 3. WIN
#include <vector>
#include <optional>
#include <array>
#include <stdexcept>
#include <set>
#include <Windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <chrono>  // For high_resolution_clock
#include <thread>  // For sleep_for
#include <algorithm>
using namespace std;

#ifdef _DEBUG
#define CONSOLE
#define DEMO // (Only Houat & HMS Clyde)
#endif

#define SAFE_DELETE(ptr) if (ptr) { delete ptr; ptr = nullptr; }

#pragma warning(push)
#pragma warning(disable:6031)  // Ignore return value of function
#pragma warning(disable:4244)  // Ignore bad conversion warnings

sChrono						Chronos[10];

// WINDOW GLFW ////////////////////////////////////////
GLFWwindow                * g_hWindow                   = nullptr;      // InitWindow()
GLFWmonitor               * g_Monitor                   = nullptr;
bool                        g_IsFullscreen              = false;
uint32_t                    g_WindowX                   = 0;
uint32_t                    g_WindowY                   = 0;
uint32_t                    g_WindowW                   = 1920;
uint32_t                    g_WindowH                   = 1080;
uint32_t			        g_WindowW_2                 = 960;
uint32_t			        g_WindowH_2                 = 540;
bool						g_PendingFullscreen			= false; 
bool						g_IsRecreating				= false;
bool                        g_bPause                    = false;
wstring				        g_CaptureName;
float                       g_CaptureDisplayTime        = 0.0f;         // Remaining display time
const float                 g_CaptureMaxTime            = 3.0f;         // 3 seconds
eh::Timer			        g_TimerScene;
eh::Timer					g_Chrono;
bool						g_bChrono = false;
int							g_ChronoStep = -1;

// VULKAN ////////////////////////////////////////
VkInstance                  g_Instance                  = nullptr;       // CreateInstance()
shared_ptr<VulkanDevice>    g_Device					= nullptr;
unique_ptr<VulkanSwapChain> g_SwapChain					= nullptr;
uint32_t                    g_FramesInFlight			= 2;

VkRenderPass				g_RenderPassImGuiLoading	= nullptr;		// CreateRenderPassImGuiLoading();
VkRenderPass                g_RenderPassShadow          = nullptr;		// CreateRenderPassShadow()
VkRenderPass                g_RenderPassReflection      = nullptr;		// CreateRenderPassReflection()
VkRenderPass                g_RenderPassWake			= nullptr;		// CreateRenderPassWake()
VkRenderPass                g_RenderPassScene           = nullptr;		// CreateRenderPassScene()
VkRenderPass                g_RenderPassPostProcess     = nullptr;		// CreateRenderPassPostProcess()
VkRenderPass                g_RenderPassImGui           = nullptr;		// CreateRenderPassImGui()
VkRenderPass				g_RenderPassBridgeMask		= nullptr;		// CreateRenderPassBridgeMask()

vector<VkFramebuffer>		g_vFramebuffersImGui;						// CreateFramebuffersImGui()
VkFramebuffer               g_FramebufferShadow			= nullptr;		// CreateFramebufferShadow()
VkFramebuffer               g_FramebufferReflection		= nullptr;		// CreateFramebufferReflection()
vector<VkFramebuffer>       g_vFramebuffersSwapChain;					// CreateFramebuffersScene()
vector<VkFramebuffer>       g_vFramebuffersPostProcess;					// CreateFramebuffersPostProcess()
VkFramebuffer               g_FramebufferWake			= nullptr;		// CreateFramebufferWake()
VkFramebuffer				g_FramebufferBridgeMask		= nullptr;		// CreateFramebufferBridgeMask()

unique_ptr<VulkanTexture>   g_ColorImage				= nullptr;
unique_ptr<VulkanTexture>   g_DepthImage				= nullptr;
unique_ptr<VulkanTexture>   g_ColorImageResolve			= nullptr;
unique_ptr<VulkanTexture>   g_DepthImageResolve			= nullptr;

unique_ptr<VulkanTexture>   g_TexReflectionColor		= nullptr;
unique_ptr<VulkanTexture>   g_TexReflectionDepth		= nullptr;
unique_ptr<VulkanTexture>   g_TexShadowDepth			= nullptr;
VkSampler                   g_TexShadowDepthSampler		= nullptr;
uint32_t                    g_ShadowWidth				= 8192;
uint32_t                    g_ShadowHeight				= 8192;
unique_ptr<VulkanTexture>   g_TexWake0					= nullptr;
unique_ptr<VulkanTexture>   g_TexWake1					= nullptr;
unique_ptr<VulkanTexture>   g_TexWake2					= nullptr;
int							g_WakeSize					= 1024;
unique_ptr<VulkanTexture>	g_TexBridgeMask				= nullptr;
VkImageView					g_TexBridgeMaskFramebufferView = nullptr;
VkSampler					g_TexBridgeMaskSampler		= nullptr;
unique_ptr<VulkanTexture>	g_TexBridgeMaskR8			= nullptr;
VkBuffer					g_BridgeMaskStagingBuffer	= nullptr;
VkDeviceMemory				g_BridgeMaskStagingMemory	= nullptr;



vector<VkCommandBuffer>     g_vCommandBuffers;							// CreateCommandBuffers()
vector<VkSemaphore>         g_vImageAvailableSemaphores;				// CreateSyncObjects()
vector<VkSemaphore>         g_vRenderFinishedSemaphores;
vector<VkFence>             g_vImageFences;
uint32_t                    g_iCurrentFrame             = 0;			// Render()

VkQueryPool                 g_QueryPool;
chrono::high_resolution_clock::time_point g_LastUpdateTime;
#define NCHRONOS    10
#define NCHRONOS_1  9
#define NTT         (2 * NCHRONOS_1)
uint64_t					g_TimestampAccumulators[NCHRONOS] = {};		// Sums of ns
uint64_t					g_TimestampCounts[NCHRONOS] = { };			// Number of samples
pair<string, uint64_t>      g_SceneTimeStamps[NCHRONOS];
bool						g_FirstRun					= true;

// DEBUG VULKAN ////////////////////////////////////////
VkDebugUtilsMessengerEXT_T* g_DebugMessenger            = nullptr;

// Fps counter
double                      g_FpsTime                   = 0.0;
int                         g_FpsFrameCount             = 0;
int                         g_nFps                      = 0;
bool					    g_bTargetFps                = false;
const double                g_FpsTarget                 = 120.0;

// SHIP //////////////////////////////////////////
unique_ptr<Ship>			g_Ship;										// Ship selected
vector<sShip>				g_vShips;									// List of ships
int							g_NoShip					= 0;			// Index in the list of the ships
unique_ptr<UdpSender>		g_UdpSender					= nullptr;		// Class to send the NMEA sentences
bool						g_bShip						= true;			// Display the ship
bool						g_bShipShadow				= true;			// Display the shadow of the ship
bool						g_bShipReflection			= true;			// Display the reflection of the ship on water
bool						g_bShipWake					= true;			// Display the wake (texture around the ship)
bool                        g_bShipKelvinWake			= true; 
int							g_LowMass					= 0;			// Half of the mass (for ImGui selection purpose)
int							g_HighMass					= 0;			// Double of the mass (for ImGui selection purpose)
bool						g_bReset					= true;			// Inhibit motion during a short period
int							g_PendingNoShipChange		= -1;			// Index in the list of the ships (to be changed when rendering if finished)

// INTERFACE ////////////////////////////////////////
vec4						g_CtrlPanel					= vec4(0.0f);	// Control zone: top left = xy, bottom right = zw
vec4						g_CtrlThrottle1				= vec4(0.0f);
float						g_CtrlThrottleHigh1			= 0.0f;
float						g_CtrlThrottleLow1			= 0.0f;
vec4						g_CtrlThrottle2				= vec4(0.0f);
float						g_CtrlThrottleHigh2			= 0.0f;
float						g_CtrlThrottleLow2			= 0.0f;
vec4						g_CtrlThrottle12			= vec4(0.0f);
float						g_CtrlThrottleHigh12		= 0.0f;
float						g_CtrlThrottleLow12			= 0.0f;
vec4						g_CtrlRudder				= vec4(0.0f);
float						g_CtrlRudderLeft			= 0.0f;
float						g_CtrlRudderRight			= 0.0f;
vec4						g_CtrlBowThruster			= vec4(0.0f);
float						g_CtrlBowThrusterLeft		= 0.0f;
float						g_CtrlBowThrusterRight		= 0.0f;
vec4						g_CtrlSternThruster			= vec4(0.0f);
float						g_CtrlSternThrusterLeft		= 0.0f;
float						g_CtrlSternThrusterRight	= 0.0f;

vec4						g_CtrlAutopilotCMD			= vec4(0.0f);
vec4						g_CtrlAutopilotM1			= vec4(0.0f);
vec4						g_CtrlAutopilotM10			= vec4(0.0f);
vec4						g_CtrlAutopilotP1			= vec4(0.0f);
vec4						g_CtrlAutopilotP10			= vec4(0.0f);
vec4						g_CtrlTimeHour				= vec4(0.0f);
vec4						g_CtrlTimeMinute			= vec4(0.0f);
vec4						g_CtrlWind					= vec4(0.0f);
vec4						g_CtrlNow					= vec4(0.0f);
vec4						g_CtrlSound					= vec4(0.0f);
vec4						g_CtrlTimer					= vec4(0.0f);
vec4						g_CtrlMto0					= vec4(0.0f);
vec4						g_CtrlMto1					= vec4(0.0f);
vec4						g_CtrlMto2					= vec4(0.0f);
vec4						g_CtrlMto3					= vec4(0.0f);
vec4						g_CtrlBollard				= vec4(0.0f);
vec4						g_CtrlAnchor				= vec4(0.0f);

// MODELS ////////////////////////////////////////
unique_ptr<Model>			g_Axis                      = nullptr;
unique_ptr<Model>			g_ArrowWind;
unique_ptr<Model>			g_BallRed[5]				= {};
unique_ptr<Model>			g_BallGreen[4]				= {};
bool						g_bShowManoeuver			= false;
unique_ptr<GridMesh>		g_Grid						= nullptr;

// OCEAN ////////////////////////////////////////
unique_ptr<Ocean>           g_Ocean                     = nullptr;
float                       g_TWS_Kt					= 15.0f;
float                       g_TWS_Deg					= 0.0f;
vec2                        g_Wind;

// SKY ////////////////////////////////////////
unique_ptr<Sky>             g_Sky						= nullptr;

// CLOUDS ////////////////////////////////////////
unique_ptr<Clouds>			g_Clouds					= nullptr;

// VIEWS ////////////////////////////////////////
vec2						g_InitialPosition			= vec2(-2.94097114, 47.38162231);
vector<sPositions>			g_vPositions;								// List of positions to be used by the ship
int							g_NoPosition				= 0;			// The number of the position in the list		
Camera                      g_Camera;
eBridgeView			        g_eBridgeView				= eBridgeView::WHEEL;// Index of the view (1,2,3) for the BRIDGE mode of the camera
bool						g_bBinoculars				= false;
bool						g_bLowIntensity				= false;
bool						g_bNightVision				= false;

// TERRAINS ////////////////////////////////////////
bool						g_bShowTerrain				= true;
vector<sTerrain>			g_vTerrains;
int							g_idxHouat					= -1;
unique_ptr<Model>			g_Port						= nullptr;
vector<sLine>				g_vPortLines;
int							g_idxHoedic					= -1;
unique_ptr<Model>			g_Pier						= nullptr;

unique_ptr<Lighthouses>		g_Lighthouses				= nullptr;
unique_ptr<Markup>			g_Markup					= nullptr;
unique_ptr<Traffics>		g_Traffics					= nullptr;

//#define BOUNDS
#ifdef BOUNDS
float XMIN = std::numeric_limits<float>::max();
float XMAX = std::numeric_limits<float>::lowest();
float ZMIN = std::numeric_limits<float>::max();
float ZMAX = std::numeric_limits<float>::lowest(); 
#endif

// SOUNDS ////////////////////////////////////////
SoundManager			  * SoundManager::instance		= nullptr;		// Initialization of the static pointer (otherwise, place it in a Sound.cpp file)
SoundManager			  * g_SoundMgr					= nullptr;
unique_ptr<Sound>			g_SoundSeagull[8];
unique_ptr<Sound>			g_SoundHorn;
bool						g_bSoundSeagull				= false;
unique_ptr<Sound>			g_SoundRain;

// POST PROCESSING ////////////////////////////////////////
unique_ptr<ScreenQuad>		g_ScreenQuad				= nullptr;
bool						g_bAboveWater				= true;

// IMGUI ////////////////////////////////////////
VkDescriptorPool            g_ImGuiDescriptorPool		= nullptr;
ImFont					  * g_FontArial08				= nullptr;
ImFont					  * g_FontArial10				= nullptr;
ImFont					  * g_FontArial12				= nullptr;
ImFont					  * g_FontArial14				= nullptr;
ImFont					  * g_FontArial16				= nullptr;
ImFont					  * g_FontArial20				= nullptr;
ImFont					  * g_FontArial24				= nullptr;
ImFont					  * g_FontArial36				= nullptr;
ImFont					  * g_FontCaveat36				= nullptr;
ImFont					  * g_FontCaveat72				= nullptr;

bool						g_bShowShortcuts			= false;		// [ F1 ]
bool						g_bShowSceneWindow			= false;		// [ F2 ]
bool						g_bShowShipWindow			= false;		// [ F3 ]
bool						g_bShowStatusBar			= false;		// [ F4 ]
bool						g_bShowAutopilotWindow		= false;		// [ F5 ]
bool						g_bShowShipForcesWindows	= false;		// [ F6 ]
bool						g_bShowChronoWindow			= false;		// [ F7 ]
bool						g_bShowOceanAnalysisWindow	= false;

// OVERLAY ///////////////////////////////////////////
bool						g_bShowTextures				= false;
unique_ptr<Overlay>			g_OverlayDisplacement		= nullptr;
unique_ptr<Overlay>			g_OverlayGradient			= nullptr;
unique_ptr<Overlay>			g_OverlayFoamBuffer = nullptr;

unique_ptr<VulkanTexture>	g_ImgSplashScreen;

unique_ptr<VulkanTexture>	g_ImgClock;
unique_ptr<VulkanTexture>	g_ImgSoundOff;
unique_ptr<VulkanTexture>	g_ImgSoundUp;
unique_ptr<VulkanTexture>	g_ImgTimer;
unique_ptr<VulkanTexture>	g_ImgMto0;
unique_ptr<VulkanTexture>	g_ImgMto1;
unique_ptr<VulkanTexture>	g_ImgMto2;
unique_ptr<VulkanTexture>	g_ImgMto3;
unique_ptr<VulkanTexture>	g_ImgBollard;
unique_ptr<VulkanTexture>	g_ImgAnchor;

ImU32			g_ColorBlack	= IM_COL32(0, 0, 0, 255);
ImU32			g_ColorGray192	= IM_COL32(192, 192, 192, 255);
ImU32			g_ColorWhite	= IM_COL32(255, 255, 255, 255);

ImU32			g_ColorRed		= IM_COL32(200, 0, 0, 255);
ImU32			g_ColorGreen	= IM_COL32(0, 200, 0, 255);
ImU32			g_ColorBlue		= IM_COL32(0, 0, 200, 255);
ImU32			g_ColorAmbre	= IM_COL32(255, 165, 0, 255);
ImU32			g_ColorCyan		= IM_COL32(0, 255, 255, 255);
ImU32			g_ColorMagenta	= IM_COL32(255, 0, 255, 255);
ImU32			g_ColorYellow	= IM_COL32(255, 255, 0, 255);

ImU32			g_ColorSimShip1 = IM_COL32(37, 87, 131, 255);
ImU32			g_ColorSimShip2 = IM_COL32(44, 129, 169, 255);
ImU32			g_ColorSimShip3 = IM_COL32(118, 150, 175, 255);
ImU32			g_ColorSimShip4 = IM_COL32(184, 199, 208, 255);


/////////////////////////////////////////////////////
vector<pair<string, string>> vShortcuts = {
	{ "C", "Orbital camera" },
	{ "B", "Bridge camera" },
	{ "WSADX", "Bridge views" },
	{ "F", "FPS camera" },
	{ "W", "Forward" },
	{ "S", "Back" },
	{ "A", "Left" },
	{ "D", "Right" },
	{ "Q", "Up" },
	{ "E", "Down" },
	{ "I", "Interpolation of camera" },
	{ "R click", "Binoculars" },
	{ "N", "Night vision" },
	{ "T", "Textures 1 debug" },
	{ "I", "Textures 2 debug" },
	{ "Space", "Pause" },
	{ "Esc", "Quit" },
	{ "F2", "Scene settings" },
	{ "F3", "Ship settings" },
	{ "F4", "Status bar" },
	{ "F5", "Autopilot settings" },
	{ "F6", "Ship Forces" },
	{ "F8", "Window capture" },
	{ "F11", "Full screen" },
	{ "O", "Color of the Ocean"},
	{ "1 ... 0", "Select ship #" },
	{ "NUM 7", "Engine Left +" },
	{ "NUM 4", "Engine Left stop" },
	{ "NUM 1", "Engine Left -" },
	{ "NUM 8", "Both engines +" },
	{ "NUM 5", "Both engines stop" },
	{ "NUM 2", "Both engines -" },
	{ "NUM 9", "Engine Right +" },
	{ "NUM 6", "Engine Right stop" },
	{ "NUM 6", "Engine Right -" },
	{ "Ins", "Bow Thruster left" },
	{ "Home", "Bow Thruster stop" },
	{ "Page up", "Bow Thruster right" },
	{ "Del", "Stern Thruster left" },
	{ "End", "Stern Thruster stop" },
	{ "Page dn", "Stern Thruster right" },
	{ "Left", "Rudder left" },
	{ "Down", "Rudder stop" },
	{ "Right", "Rudder right" },
	{ "L", "Ship lights" },
	{ "H", "Ship horn" },
	{ "NUM /", "Both engine 7/10" },
	{ "NUM +", "Increase speed" },
	{ "NUM -", "Decrease speed" },
};

void ApplyTheme();
void SetPosition();
void SetHeading(float heading);
void SetShip(int n);
void SetMeteo(int n);
void PartialRecreate();