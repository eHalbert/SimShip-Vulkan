#include "SimShip.h"

int RENDER_SKY      = 2;
int RENDER_OCEAN    = 3;   // 0 = wireframe, 1 = one mesh, 2 = lod, 3 = lod with wake
int IMGUI_STYLE     = 4;

extern pair<string, uint64_t> OceanTimeStamps[5];

/*
RENDER PASS 0 : g_RenderPassShadow (1x)
├── 1 ATTACHMENT    : g_TexShadowDepth (1x)       → Texture for Ocean Shader & Model shader
└── 1 FRAMEBUFFER   : g_ShadowFramebuffer

RENDER PASS 1 : g_RenderPassWake (1x)
├── 1 ATTACHMENT    : g_TexWake (1x)              → Texture for Ocean Shader
└── 1 FRAMEBUFFER   : g_WakeFramebuffer

RENDER PASS 2 : g_RenderPassReflection (1x)
├── 2 ATTACHMENTS   : g_TexReflectionColor (1x)   → Texture for Ocean Shader
│                   : g_TexReflectionDepth (1x)   → Depth pour reflection
└── 1 FRAMEBUFFER   : g_ReflectionFramebuffer

ATMOSPHERE : Compute shaders (hors render pass)
├── Compute 3 LUTs (Bruneton/Sakmary)
├── Compute Sky → g_SkyImage[g_iCurrentFrame]
├── Compute Clouds → Texture
└── Assemble sky+clouds → g_Clouds PostProcess Texture

RENDER PASS 3 : g_RenderPassScene (5 ATTACHMENTS, 1 SUBPASS)
├── ATTACHMENT 0 : g_ColorImageView (MSAA 8x)     → Rendu COLOR MSAA
├── ATTACHMENT 1 : g_vSwapChainImageViews[i] (1x) → Resolve COLOR (temporaire)
├── ATTACHMENT 2 : g_DepthImageView (MSAA 8x)     → Rendu DEPTH MSAA
├── ATTACHMENT 3 : g_DepthViewResolve (1x)        → Resolve DEPTH (ScreenQuad)
└── ATTACHMENT 4 : g_ColorViewResolve (1x)        → ScreenQuad COLOR input
└── 2 FRAMEBUFFERS : g_vFramebuffersSwapChain[0..1]

RENDER PASS 4 : g_RenderPassPostProcess (1x)
├── 1 ATTACHMENT    : g_vSwapChainImageViews[i] (1x) → LOAD/STORE final
└── 2 FRAMEBUFFERS  : g_vFramebuffersPostProcess[0..1]

RENDER PASS 5 : g_RenderPassImGui (1x)
├── 1 ATTACHMENT    : g_vSwapChainImageViews[i] (1x) → LOAD/STORE (over post-process)
└── 2 FRAMEBUFFERS  : g_vFramebuffersPostProcess[0..1] (réutilisés)
*/

void SwitchToFullScreen()
{
    vkDeviceWaitIdle(g_Device->device);

    static uint32_t oldWindowX;
    static uint32_t oldWindowY;
    static uint32_t oldWindowW;
    static uint32_t oldWindowH;

    if (!g_IsFullscreen)    // -> Switch to fullscreen
    {
        // Storing the window position BEFORE fullscreen, glfwGetWindowPos returns the top-left corner of the client area
        int wx, wy;
        glfwGetWindowPos(g_hWindow, &wx, &wy);
        oldWindowX = wx;
        oldWindowY = wy;
        oldWindowW = g_WindowW;
        oldWindowH = g_WindowH;

        GLFWmonitor* targetMonitor = get_current_monitor(g_hWindow);
        const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
        glfwSetWindowMonitor(g_hWindow, targetMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else
    {
        // Restoring directly with the saved coordinates without adjustment by GetWindowFrameSize (unreliable post-fullscreen)
        glfwSetWindowMonitor(g_hWindow, nullptr, oldWindowX, oldWindowY, oldWindowW, oldWindowH, 0);
    }

    g_IsFullscreen = !g_IsFullscreen;
}
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // Callback function for window resizing
    if (width == 0 || height == 0) return;
    if (g_IsRecreating) return;

    g_WindowW = width;
    g_WindowH = height;
    g_WindowW_2 = width / 2;
    g_WindowH_2 = height / 2;

    g_Camera.SetViewportSize(g_WindowW, g_WindowH);
    
    g_IsRecreating = true; 
    PartialRecreate();
    g_IsRecreating = false;
}
void CursorPosCallback(GLFWwindow* window, double xposIn, double yposIn)
{
    g_Camera.MousePosUpdate(xposIn, yposIn);
}
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    vec2 mouse = vec2(int(xpos), int(ypos));
    if (button == 0 && action == GLFW_PRESS)
    {
        if (IsInRect(g_CtrlSound, mouse))
        {
            g_SoundMgr->bSound = !g_SoundMgr->bSound;
        }
        if (IsInRect(g_CtrlTimer, mouse))
        {
            g_ChronoStep++;
            if (g_ChronoStep > 2) g_ChronoStep = 0;
            switch (g_ChronoStep)
            {
            case 0: g_bChrono = true; g_Chrono.restart();  break;
            case 1: g_Chrono.stop(); break;
            case 2: g_bChrono = false; break;
            }
        }
        
        if (IsInRect(g_CtrlMto0, mouse))    // Random
            SetMeteo(0);
        if (IsInRect(g_CtrlMto1, mouse))    // Clear
            SetMeteo(1);
        if (IsInRect(g_CtrlMto2, mouse))    // Cloudy
            SetMeteo(2);
        if (IsInRect(g_CtrlMto3, mouse))    // Foggy
            SetMeteo(3);
        
        if (IsInRect(g_CtrlThrottle1, mouse))
        {
            float valeur = 1 - (mouse.y - g_CtrlThrottleHigh1) / (g_CtrlThrottleLow1 - g_CtrlThrottleHigh1);
            g_Ship->PowerCurrentStep1 = (valeur - 0.5f) * (2.0f * g_Ship->ship.PowerStepMax);
        }
        if (IsInRect(g_CtrlThrottle2, mouse))
        {
            float valeur = 1 - (mouse.y - g_CtrlThrottleHigh2) / (g_CtrlThrottleLow2 - g_CtrlThrottleHigh2);
            g_Ship->PowerCurrentStep2 = (valeur - 0.5f) * (2.0f * g_Ship->ship.PowerStepMax);
        }
        if (IsInRect(g_CtrlBowThruster, mouse))
        {
            float valeur = (mouse.x - g_CtrlBowThrusterLeft) / (g_CtrlBowThrusterRight - g_CtrlBowThrusterLeft);
            g_Ship->BowThrusterCurrentStep = (valeur - 0.5f) * (2.0f * g_Ship->ship.BowThrusterStepMax);
        }
        if (IsInRect(g_CtrlSternThruster, mouse))
        {
            float valeur = (mouse.x - g_CtrlSternThrusterLeft) / (g_CtrlSternThrusterRight - g_CtrlSternThrusterLeft);
            g_Ship->SternThrusterCurrentStep = (valeur - 0.5f) * (2.0f * g_Ship->ship.SternThrusterStepMax);
        }
        if (IsInRect(g_CtrlRudder, mouse))
        {
            float valeur = 1 - (mouse.x - g_CtrlRudderLeft) / (g_CtrlRudderRight - g_CtrlRudderLeft);
            g_Ship->RudderCurrentStep = (valeur - 0.5f) * (2.0f * g_Ship->ship.RudderStepMax);
        }
        
        if (IsInRect(g_CtrlNow, mouse))
        {
            g_Sky->SetNow();
        }

        if (IsInRect(g_CtrlAutopilotCMD, mouse))
        {
            g_Ship->bAutopilot = !g_Ship->bAutopilot;
            return;
        }
        if (IsInRect(g_CtrlAutopilotM1, mouse))
        {
            g_Ship->HDGInstruction--;
            if (g_Ship->HDGInstruction < 0)
                g_Ship->HDGInstruction += 360;
            return;
        }
        if (IsInRect(g_CtrlAutopilotP1, mouse))
        {
            g_Ship->HDGInstruction++;
            if (g_Ship->HDGInstruction > 360)
                g_Ship->HDGInstruction -= 360;
            return;
        }
        if (IsInRect(g_CtrlAutopilotM10, mouse))
        {
            g_Ship->HDGInstruction -= 10;
            if (g_Ship->HDGInstruction < 0)
                g_Ship->HDGInstruction += 360;
            return;
        }
        if (IsInRect(g_CtrlAutopilotP10, mouse))
        {
            g_Ship->HDGInstruction += 10;
            if (g_Ship->HDGInstruction > 360)
                g_Ship->HDGInstruction -= 360;
            return;
        }
    }

    if (button == 1 && action == GLFW_PRESS)
        g_bBinoculars = !g_bBinoculars;

    if (!ImGui::GetIO().WantCaptureMouse)
        g_Camera.MouseButtonUpdate(button, action, mods);
}
void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    vec2 mouse(mouseX, mouseY);
    if (IsInRect(g_CtrlPanel, mouse))
    {
        if (IsInRect(g_CtrlThrottle1, mouse))
        {
            if (yoffset > 0)
            {
                g_Ship->PowerCurrentStep1++;
                if (g_Ship->PowerCurrentStep1 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax;
            }
            else
            {
                g_Ship->PowerCurrentStep1--;
                if (g_Ship->PowerCurrentStep1 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = -g_Ship->ship.PowerStepMax;
            }
            return;
        }
        else if (IsInRect(g_CtrlThrottle2, mouse))
        {
            if (yoffset > 0)
            {
                g_Ship->PowerCurrentStep2++;
                if (g_Ship->PowerCurrentStep2 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax;
            }
            else
            {
                g_Ship->PowerCurrentStep2--;
                if (g_Ship->PowerCurrentStep2 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = -g_Ship->ship.PowerStepMax;
            }
            return;
        }
        else if (IsInRect(g_CtrlThrottle12, mouse))
        {
            int average = (g_Ship->PowerCurrentStep1 + g_Ship->PowerCurrentStep2) / 2;
            if (yoffset > 0)
			{
				g_Ship->PowerCurrentStep1 = average + 1;
				g_Ship->PowerCurrentStep2 = average + 1;
				if (g_Ship->PowerCurrentStep1 > g_Ship->ship.PowerStepMax)
					g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax;
				if (g_Ship->PowerCurrentStep2 > g_Ship->ship.PowerStepMax)
					g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax;
			}
			else
			{
                g_Ship->PowerCurrentStep1 = average - 1;
                g_Ship->PowerCurrentStep2 = average - 1;
                if (g_Ship->PowerCurrentStep1 < -g_Ship->ship.PowerStepMax)
					g_Ship->PowerCurrentStep1 = -g_Ship->ship.PowerStepMax;
				if (g_Ship->PowerCurrentStep2 < -g_Ship->ship.PowerStepMax)
					g_Ship->PowerCurrentStep2 = -g_Ship->ship.PowerStepMax;
			}
			return;
        }
        else if (IsInRect(g_CtrlRudder, mouse))
        {
            if (yoffset < 0)
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->RudderCurrentStep++;
                if (g_Ship->RudderCurrentStep > g_Ship->ship.RudderStepMax)
                    g_Ship->RudderCurrentStep = g_Ship->ship.RudderStepMax;
            }
            else
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->RudderCurrentStep--;
                if (g_Ship->RudderCurrentStep < -g_Ship->ship.RudderStepMax)
                    g_Ship->RudderCurrentStep = -g_Ship->ship.RudderStepMax;
            }
            return;
        }
        else if (IsInRect(g_CtrlBowThruster, mouse))
        {
            if (yoffset > 0)
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->BowThrusterCurrentStep++;
                if (g_Ship->BowThrusterCurrentStep > g_Ship->ship.BowThrusterStepMax)
                    g_Ship->BowThrusterCurrentStep = g_Ship->ship.BowThrusterStepMax;
            }
            else
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->BowThrusterCurrentStep--;
                if (g_Ship->BowThrusterCurrentStep < -g_Ship->ship.BowThrusterStepMax)
                    g_Ship->BowThrusterCurrentStep = -g_Ship->ship.BowThrusterStepMax;
            }
            return;
        }
        else if (IsInRect(g_CtrlSternThruster, mouse))
        {
            if (yoffset > 0)
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->SternThrusterCurrentStep++;
                if (g_Ship->SternThrusterCurrentStep > g_Ship->ship.SternThrusterStepMax)
                    g_Ship->SternThrusterCurrentStep = g_Ship->ship.SternThrusterStepMax;
            }
            else
            {
                if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
                g_Ship->SternThrusterCurrentStep--;
                if (g_Ship->SternThrusterCurrentStep < -g_Ship->ship.SternThrusterStepMax)
                    g_Ship->SternThrusterCurrentStep = -g_Ship->ship.SternThrusterStepMax;
            }
            return;
        }
        else if (IsInRect(g_CtrlTimeHour, mouse))
        {
            sHM hm = g_Sky->GetTime();
            if (yoffset < 0)
            {
                hm.hour--;
                if (hm.hour < 0.0f)
                    hm.hour = 0.0f;
            }
            else
            {
                hm.hour++;
                if (hm.hour > 23.0f)
                    hm.hour = 23.0f;
            }
            g_Sky->SetTime(hm.hour, hm.minute);
            g_Ship->bLights = g_Sky->SunPosition.y < 0.0f ? true : false;
            g_Traffics->bLights = g_Ship->bLights;
            return;
        }
        else if (IsInRect(g_CtrlTimeMinute, mouse))
        {
            sHM hm = g_Sky->GetTime();
            if (yoffset < 0)
            {
                hm.minute--;
                if (hm.minute < 0)
                {
                    if (hm.hour > 0)
                    {
                        hm.minute = 59;
                        hm.hour--;
                    }
                    else
                        hm.minute = 0;
                }
            }
            else
            {
                hm.minute++;
                if (hm.minute > 59)
                {
                    if (hm.hour < 23)
                    {
                        hm.minute = 0;
                        hm.hour++;
                    }
                    else
                        hm.minute = 59;
                }
            }
            g_Sky->SetTime(hm.hour, hm.minute);
            g_Ship->bLights = g_Sky->SunPosition.y < 0.0f ? true : false;
            g_Traffics->bLights = g_Ship->bLights;
            return;
        }
        else if (IsInRect(g_CtrlWind, mouse))
        {
            if (yoffset < 0)
                g_TWS_Kt--;
            else
                g_TWS_Kt++;
            g_TWS_Kt = glm::clamp(g_TWS_Kt, 0.1f, 30.0f);
            g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
            g_Ocean->SetWind(g_Wind);
            g_Ocean->InitFrequencies();
            g_Clouds->SetCloudSpeed(g_TWS_Kt);
            return;
        }
        else if (IsInRect(g_CtrlAutopilotM1, mouse))
        {
            if (yoffset < 0)
            {
                g_Ship->HDGInstruction--;
                if (g_Ship->HDGInstruction < 0)
                    g_Ship->HDGInstruction += 360;
            }
            if (yoffset > 0)
            {
                g_Ship->HDGInstruction++;
                if (g_Ship->HDGInstruction > 360)
                    g_Ship->HDGInstruction -= 360;
            }
            return;
        }
        if (IsInRect(g_CtrlAutopilotP1, mouse))
        {
            if (yoffset < 0)
            {
                g_Ship->HDGInstruction--;
                if (g_Ship->HDGInstruction < 0)
                    g_Ship->HDGInstruction += 360;
            }
            if (yoffset > 0)
            {
                g_Ship->HDGInstruction++;
                if (g_Ship->HDGInstruction > 360)
                    g_Ship->HDGInstruction -= 360;
            }
            return;
        }
        if (IsInRect(g_CtrlAutopilotM10, mouse))
        {
            if (yoffset < 0)
            {
                g_Ship->HDGInstruction -= 10;
                if (g_Ship->HDGInstruction < 0)
                    g_Ship->HDGInstruction += 360;
            }
            if (yoffset > 0)
            {
                g_Ship->HDGInstruction += 10;
                if (g_Ship->HDGInstruction > 360)
                    g_Ship->HDGInstruction -= 360;
            }
            return;
        }
        if (IsInRect(g_CtrlAutopilotP10, mouse))
        {
            if (yoffset < 0)
            {
                g_Ship->HDGInstruction -= 10;
                if (g_Ship->HDGInstruction < 0)
                    g_Ship->HDGInstruction += 360;
            }
            if (yoffset > 0)
            {
                g_Ship->HDGInstruction += 10;
                if (g_Ship->HDGInstruction > 360)
                    g_Ship->HDGInstruction -= 360;
            }
            return;
        }
    }
    else if (!ImGui::GetIO().WantCaptureMouse)
    {
        if (g_Camera.GetMode() == eCameraMode::ORBITAL)
            g_Camera.AdjustOrbitRadius(-yoffset * 0.1f * g_Camera.GetOrbitRadius());
    }
}
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    g_Camera.KeyboardUpdate(key, scancode, action, mods);
    
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        switch (key)
        {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, true);
            break;
            // Pause
        case GLFW_KEY_SPACE:
            g_bPause = !g_bPause;
            if (g_bPause)   g_TimerScene.stop();
            else            g_TimerScene.start();
            break;
        case GLFW_KEY_1:
            SetShip(0);
            break;
        case GLFW_KEY_2:
            SetShip(1);
            break;
        case GLFW_KEY_3:
            SetShip(2);
            break;
        case GLFW_KEY_4:
            SetShip(3);
            break;
        case GLFW_KEY_5:
            SetShip(4);
            break;
        case GLFW_KEY_6:
            SetShip(5);
            break;
        case GLFW_KEY_7:
            SetShip(6);
            break;
        case GLFW_KEY_8:
            SetShip(7);
            break;
        case GLFW_KEY_9:
            SetShip(8);
            break;
        case GLFW_KEY_0:
            SetShip(9);
            break;
        case GLFW_KEY_A:
            if (g_Camera.GetMode() != eCameraMode::FPS)
            {
                g_eBridgeView = eBridgeView::LEFT;
                g_Camera.KeyboardUpdate(GLFW_KEY_B, scancode, action, mods);
            }
            break;
        case GLFW_KEY_D:
            if (g_Camera.GetMode() != eCameraMode::FPS)
            {
                g_eBridgeView = eBridgeView::RIGHT;
                g_Camera.KeyboardUpdate(GLFW_KEY_B, scancode, action, mods);
            }
            break;
        case GLFW_KEY_G:
            IMGUI_STYLE++;
			ApplyTheme();
			break;
        case GLFW_KEY_H:    // Horn
            if (g_SoundMgr->bSound && g_Ship->bSound)
                g_SoundHorn->play();
            break;
        case GLFW_KEY_I:    // Cycle the interpolation function to the next type
        {
            int current = static_cast<int>(g_Camera.GetInterpolation());
            int next = (current + 1) % static_cast<int>(eInterpolation::COUNT);
            g_Camera.SetInterpolation(static_cast<eInterpolation>(next));
        }
        break;
        case GLFW_KEY_L:
            g_Ship->bLights = !g_Ship->bLights;
            g_Traffics->bLights = g_Ship->bLights;
            break;
        case GLFW_KEY_SEMICOLON:    // M for Manoeuver
            g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax * 7 / 10;
            g_Ship->PropRpm1 = g_Ship->ship.PropRpmMax * 7 / 10;
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax * 7 / 10;
                g_Ship->PropRpm2 = g_Ship->ship.PropRpmMax * 7 / 10;
            }
            g_Ship->SurgeVelocity = knot_to_ms(g_Ship->ship.SpeedEcoKt);
            g_Ship->RudderCurrentStep = -g_Ship->ship.RudderStepMax;
            break;
        case GLFW_KEY_N:
            g_bNightVision = !g_bNightVision;
            break;
        case GLFW_KEY_O:    // Color ocean
            g_Ocean->iOceanColor++;
            if (g_Ocean->iOceanColor >= g_Ocean->vOceanColors.size())
                g_Ocean->iOceanColor = 0;
            g_Ocean->OceanColor = g_Ocean->vOceanColors[g_Ocean->iOceanColor];
            break;
        case GLFW_KEY_P:
            RENDER_OCEAN++;
            if (RENDER_OCEAN == 4)
                RENDER_OCEAN = 0;
            break;
        case GLFW_KEY_S:
            if (g_Camera.GetMode() != eCameraMode::FPS)
            {
                g_eBridgeView = eBridgeView::WHEEL;
                g_Camera.KeyboardUpdate(GLFW_KEY_B, scancode, action, mods);
            }
            break;
        case GLFW_KEY_T:    // Textures
            g_bShowTextures = !g_bShowTextures;
            break;
        case GLFW_KEY_W:
            if (g_Camera.GetMode() != eCameraMode::FPS)
            {
                g_eBridgeView = eBridgeView::BOW;
                g_Camera.KeyboardUpdate(GLFW_KEY_B, scancode, action, mods);
            }
            break;
        case GLFW_KEY_X:
            if (g_Camera.GetMode() != eCameraMode::FPS)
            {
                g_eBridgeView = eBridgeView::STERN;
                g_Camera.KeyboardUpdate(GLFW_KEY_B, scancode, action, mods);
            }
            break;
        case GLFW_KEY_KP_SUBTRACT:  // 1 kt
            g_Ship->SurgeVelocity -= 0.5144f;   
            break;
        case GLFW_KEY_KP_ADD:       // 1 kt
            g_Ship->SurgeVelocity += 0.5144f;   
            break;
        case GLFW_KEY_KP_DIVIDE:    // Engine at 7/10
            g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax * 7 / 10;
            g_Ship->PropRpm1 = g_Ship->ship.PropRpmMax * 7 / 10;
            g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax * 7 / 10;
            g_Ship->PropRpm2 = g_Ship->ship.PropRpmMax * 7 / 10;
            break;
		case GLFW_KEY_KP_MULTIPLY:  // Service speed
            g_Ship->SurgeVelocity = knot_to_ms(g_Ship->ship.SpeedEcoKt);
            break;
        case GLFW_KEY_LEFT:         // Rudder
            if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
            g_Ship->RudderCurrentStep++;
            if (g_Ship->RudderCurrentStep > g_Ship->ship.RudderStepMax)
                g_Ship->RudderCurrentStep = g_Ship->ship.RudderStepMax;
            break;
        case GLFW_KEY_DOWN:
            if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
            g_Ship->RudderCurrentStep = 0;
            break;
        case GLFW_KEY_RIGHT:
            if (g_Ship->bAutopilot) g_Ship->bAutopilot = false;
            g_Ship->RudderCurrentStep--;
            if (g_Ship->RudderCurrentStep < -g_Ship->ship.RudderStepMax)
                g_Ship->RudderCurrentStep = -g_Ship->ship.RudderStepMax;
            break;
        case GLFW_KEY_KP_7:         // Power LEFT
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep1++;
                if (g_Ship->PowerCurrentStep1 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax;
            }
            break;
        case GLFW_KEY_KP_4:
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep1 = 0;
            }
            break;
        case GLFW_KEY_KP_1:
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep1--;
                if (g_Ship->PowerCurrentStep1 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = -g_Ship->ship.PowerStepMax;
            }
            break;
        case GLFW_KEY_KP_9:         // Power RIGHT
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep2++;
                if (g_Ship->PowerCurrentStep2 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax;
            }
            break;
        case GLFW_KEY_KP_6:
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep2 = 0;
            }
            break;
        case GLFW_KEY_KP_3:
            if (g_Ship->ship.nPropeller == 2)
            {
                g_Ship->PowerCurrentStep2--;
                if (g_Ship->PowerCurrentStep2 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = -g_Ship->ship.PowerStepMax;
            }
            break;
		case GLFW_KEY_KP_8: // Power ALL or single prop
            if (g_Ship->ship.nPropeller == 2)
            {
                int average = (g_Ship->PowerCurrentStep1 + g_Ship->PowerCurrentStep2) / 2;
                g_Ship->PowerCurrentStep1 = average + 1;
                if (g_Ship->PowerCurrentStep1 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax;
                g_Ship->PowerCurrentStep2 = average + 1;
                if (g_Ship->PowerCurrentStep2 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = g_Ship->ship.PowerStepMax;
            }
            else
            {
                g_Ship->PowerCurrentStep1++;
                if (g_Ship->PowerCurrentStep1 > g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = g_Ship->ship.PowerStepMax;
            }
            break;
        case GLFW_KEY_KP_5:
            g_Ship->PowerCurrentStep1 = 0;
            g_Ship->PowerCurrentStep2 = 0;
            break;
        case GLFW_KEY_KP_2:
            if (g_Ship->ship.nPropeller == 2)
            {
                int average = (g_Ship->PowerCurrentStep1 + g_Ship->PowerCurrentStep2) / 2;
                g_Ship->PowerCurrentStep1 = average - 1;
                if (g_Ship->PowerCurrentStep1 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = -g_Ship->ship.PowerStepMax;
                g_Ship->PowerCurrentStep2 = average - 1;
                if (g_Ship->PowerCurrentStep2 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep2 = -g_Ship->ship.PowerStepMax;
            }
            else
            {
                g_Ship->PowerCurrentStep1--;
                if (g_Ship->PowerCurrentStep1 < -g_Ship->ship.PowerStepMax)
                    g_Ship->PowerCurrentStep1 = -g_Ship->ship.PowerStepMax;
            }
            break;
        case GLFW_KEY_INSERT:   // Bow Thruster
            if (g_Ship->ship.HasBowThruster)
            {
                g_Ship->BowThrusterCurrentStep--;
                if (g_Ship->BowThrusterCurrentStep < -g_Ship->ship.BowThrusterStepMax)
                    g_Ship->BowThrusterCurrentStep = -g_Ship->ship.BowThrusterStepMax;
            }
            break;
        case GLFW_KEY_HOME:
            if (g_Ship->ship.HasBowThruster)
            {
                g_Ship->BowThrusterCurrentStep = 0;
            }
            break;
        case GLFW_KEY_PAGE_UP:
            if (g_Ship->ship.HasBowThruster)
            {
                g_Ship->BowThrusterCurrentStep++;
                if (g_Ship->BowThrusterCurrentStep > g_Ship->ship.BowThrusterStepMax)
                    g_Ship->BowThrusterCurrentStep = g_Ship->ship.BowThrusterStepMax;
            }
            break;
        case GLFW_KEY_DELETE:   // Stern Thruster
            if (g_Ship->ship.HasSternThruster)
            {
                g_Ship->SternThrusterCurrentStep--;
                if (g_Ship->SternThrusterCurrentStep < -g_Ship->ship.SternThrusterStepMax)
                    g_Ship->SternThrusterCurrentStep = -g_Ship->ship.SternThrusterStepMax;
            }
            break;
        case GLFW_KEY_END:
            if (g_Ship->ship.HasSternThruster)
            {
                g_Ship->SternThrusterCurrentStep = 0;
            }
            break;
        case GLFW_KEY_PAGE_DOWN:
            if (g_Ship->ship.HasSternThruster)
            {
                g_Ship->SternThrusterCurrentStep++;
                if (g_Ship->SternThrusterCurrentStep > g_Ship->ship.SternThrusterStepMax)
                    g_Ship->SternThrusterCurrentStep = g_Ship->ship.SternThrusterStepMax;
            }
            break;
		case GLFW_KEY_F1:
            g_bShowShortcuts = !g_bShowShortcuts;
            break;
        case GLFW_KEY_F2:
            g_bShowSceneWindow = !g_bShowSceneWindow;
            break;
        case GLFW_KEY_F3:
            g_bShowShipWindow = !g_bShowShipWindow;
            break;
        case GLFW_KEY_F4:
            g_bShowStatusBar = !g_bShowStatusBar;
            break;
        case GLFW_KEY_F5:
            g_bShowAutopilotWindow = !g_bShowAutopilotWindow;
            break;
        case GLFW_KEY_F6:
            g_bShowShipForcesWindows = !g_bShowShipForcesWindows;
            break;
        case GLFW_KEY_F7:
            g_bShowChronoWindow = !g_bShowChronoWindow;
            break;
        case GLFW_KEY_F8:
        {
            HWND hWnd = glfwGetWin32Window(g_hWindow);
            g_CaptureName = SaveClientArea(hWnd);
            g_CaptureDisplayTime = g_CaptureMaxTime;
        }
        break;
        case GLFW_KEY_F11:
            g_PendingFullscreen = true;
            break;
        }
    }
    else if (action == GLFW_RELEASE)
    {
        // A key has just been released
    }
}

ImVec2 GetLeftAlignedTextPos(const char* text, ImVec2 box_left, float padding_x, float padding_y, ImFont* font)
{
    ImGui::PushFont(font);

    // Calculate text size
    ImVec2 text_size = ImGui::CalcTextSize(text);

    // Text position: left edge of the box + padding (left-aligned)
    ImVec2 text_pos = ImVec2(box_left.x + padding_x, box_left.y - text_size.y * 0.5f);

    ImGui::PopFont();
    return text_pos;
}
ImVec2 GetCenteredTextPos(const char* text, ImVec2 box_center, float padding_x, float padding_y, ImFont* font)
{
    ImGui::PushFont(font);

    // Calculate text size
    ImVec2 text_size = ImGui::CalcTextSize(text);

    // Text position: center of the box - half text size
    ImVec2 text_pos = ImVec2( box_center.x - text_size.x * 0.5f, box_center.y - text_size.y * 0.5f );

    ImGui::PopFont();
    return text_pos;
}
ImVec2 GetRightAlignedTextPos(const char* text, ImVec2 box_right, float padding_x, float padding_y, ImFont* font)
{
    ImGui::PushFont(font);

    // Calculate text size
    ImVec2 text_size = ImGui::CalcTextSize(text);

    // Text position: right edge of the box - text size (right-aligned)
    ImVec2 text_pos = ImVec2(box_right.x - text_size.x - padding_x, box_right.y - text_size.y * 0.5f);

    ImGui::PopFont();
    return text_pos;
}

// Imgui
void TestCtrlZone(ImDrawList* draw_list, vec4 ctrl)
{
    draw_list->AddRect(ImVec2(ctrl.x, ctrl.y), ImVec2(ctrl.x + ctrl.z, ctrl.y + ctrl.w), g_ColorMagenta, 1.0f);
}
void RenderEnv(float x, float y) 
{
    ImGui::PushFont(g_FontArial16);

    float w = 80;
    float h = 136;
    float buttonWidth = 72;
    float buttonHeight = 28;
    float gap = 5;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = ImVec2(x, y);

    // Rounded frame
    draw_list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), g_ColorSimShip4, 10.0f);
    draw_list->AddRect(pos, ImVec2(pos.x + w, pos.y + h), g_ColorWhite, 10.0f, 0, 1.0f);

    // Camera mode button
    float currentY = y + 5;
    ImU32 color = g_ColorAmbre;
    switch (g_Camera.GetMode())
    {
    case eCameraMode::ORBITAL:  color = g_ColorAmbre;  break;
    case eCameraMode::BRIDGE:   color = g_ColorRed;  break;
    case eCameraMode::FPS:      color = g_ColorGreen; break;
    }
    string mode;
    switch (g_Camera.GetMode())
    {
    case eCameraMode::ORBITAL:  mode = "ORBITAL"; break;
    case eCameraMode::BRIDGE:
        switch (g_eBridgeView)
        {
        case eBridgeView::WHEEL:    mode = "BRIDGE"; break;
        case eBridgeView::LEFT:     mode = "LEFT"; break;
        case eBridgeView::RIGHT:    mode = "RIGHT"; break;
        case eBridgeView::BOW:      mode = "BOW"; break;
        case eBridgeView::STERN:    mode = "STERN"; break;
        }
        break;
    case eCameraMode::FPS:      mode = "FPS"; break;
    }
    ImVec2 btn_pos = ImVec2(x + (w - buttonWidth) / 2, currentY);
    draw_list->AddRectFilled(btn_pos, ImVec2(btn_pos.x + buttonWidth, btn_pos.y + buttonHeight), color, 5.0f);
	ImVec2 textPos = GetCenteredTextPos(mode.c_str(), ImVec2(btn_pos.x + buttonWidth / 2, btn_pos.y + buttonHeight / 2), 0, 0, g_FontArial16);
    draw_list->AddText(textPos, g_ColorWhite, mode.c_str());

    // Lookat angle
    buttonWidth = 60; buttonHeight = 18;
    char lookat[16];
    snprintf(lookat, sizeof(lookat), "%03d\xC2\xB0", (int)g_Camera.GetNorthAngleDEG());
    ImVec2 look_pos = ImVec2(x + (w - buttonWidth) / 2, y + 38);
    draw_list->AddRectFilled(look_pos, ImVec2(look_pos.x + buttonWidth, look_pos.y + buttonHeight), g_ColorSimShip1, 3.0f);
	ImVec2 text_pos = GetCenteredTextPos(lookat, ImVec2(look_pos.x + buttonWidth / 2, look_pos.y + buttonHeight / 2), 0, 0, g_FontArial16);
    draw_list->AddText(text_pos, g_ColorCyan, lookat);

    // FPS
    char fps[16];
    snprintf(fps, sizeof(fps), "%d i/s", g_nFps);
    ImVec2 fps_pos = ImVec2(x + (w - buttonWidth) / 2, y + 61);
    draw_list->AddRectFilled(fps_pos, ImVec2(fps_pos.x + buttonWidth, fps_pos.y + buttonHeight), g_ColorSimShip1, 3.0f);
	text_pos = GetCenteredTextPos(fps, ImVec2(fps_pos.x + buttonWidth / 2, fps_pos.y + buttonHeight / 2), 0, 0, g_FontArial16);
    draw_list->AddText(text_pos, g_ColorCyan, fps);

    // Icon Sound
    ImVec2 icon_pos = ImVec2(x + 5, y + 88);
    VkDescriptorSet* tex_sound = g_SoundMgr->bSound ? (VkDescriptorSet*)g_ImgSoundUp->GetImGuiDescriptorSet() : (VkDescriptorSet*)g_ImgSoundOff->GetImGuiDescriptorSet();
    draw_list->AddRectFilled(icon_pos, ImVec2(icon_pos.x + 20, icon_pos.y + 20), g_ColorSimShip3, 5.0f);
    draw_list->AddImage((ImTextureID)tex_sound, icon_pos, ImVec2(icon_pos.x + 20, icon_pos.y + 20));
    g_CtrlSound = vec4(x + 5, 88, 20.0f, 20.0f);

    // Icon Timer
    ImVec2 timer_pos = ImVec2(x + 30, y + 88);
    draw_list->AddRectFilled(timer_pos, ImVec2(timer_pos.x + 20, timer_pos.y + 20), g_ColorSimShip3, 5.0f);
    draw_list->AddImage((ImTextureID)(VkDescriptorSet*)g_ImgTimer->GetImGuiDescriptorSet(), timer_pos, ImVec2(timer_pos.x + 20, timer_pos.y + 20));
    g_CtrlTimer = vec4(x + 30, 88, 20.0f, 20.0f);

    // Icons Meteo

    ImVec2 mto0_pos = ImVec2(x + 55, y + 88);
    draw_list->AddRectFilled(mto0_pos, ImVec2(mto0_pos.x + 20, mto0_pos.y + 20), g_ColorSimShip2, 5.0f);
    draw_list->AddImage((ImTextureID)(VkDescriptorSet*)g_ImgMto0->GetImGuiDescriptorSet(), mto0_pos, ImVec2(mto0_pos.x + 20, mto0_pos.y + 20));
    g_CtrlMto0 = vec4(x + 55, 88, 20.0f, 20.0f);

    ImVec2 mto1_pos = ImVec2(x + 5, y + 112);
    draw_list->AddRectFilled(mto1_pos, ImVec2(mto1_pos.x + 20, mto1_pos.y + 20), g_ColorSimShip2, 5.0f);
    draw_list->AddImage((ImTextureID)(VkDescriptorSet*)g_ImgMto1->GetImGuiDescriptorSet(), mto1_pos, ImVec2(mto1_pos.x + 20, mto1_pos.y + 20));
    g_CtrlMto1 = vec4(x + 5, 112, 20.0f, 20.0f);

    ImVec2 mto2_pos = ImVec2(x + 30, y + 112);
    draw_list->AddRectFilled(mto2_pos, ImVec2(mto2_pos.x + 20, mto2_pos.y + 20), g_ColorSimShip2, 5.0f);
    draw_list->AddImage((ImTextureID)(VkDescriptorSet*)g_ImgMto2->GetImGuiDescriptorSet(), mto2_pos, ImVec2(mto2_pos.x + 20, mto2_pos.y + 20));
    g_CtrlMto2 = vec4(x + 30, 112, 20.0f, 20.0f);

    ImVec2 mto3_pos = ImVec2(x + 55, y + 112);
    draw_list->AddRectFilled(mto3_pos, ImVec2(mto3_pos.x + 20, mto3_pos.y + 20), g_ColorSimShip2, 5.0f);
    draw_list->AddImage((ImTextureID)(VkDescriptorSet*)g_ImgMto3->GetImGuiDescriptorSet(), mto3_pos, ImVec2(mto3_pos.x + 20, mto3_pos.y + 20));
    g_CtrlMto3 = vec4(x + 55, 112, 20.0f, 20.0f);

    // Overlay night
    if (g_Sky->SunPosition.y < 0.0f) 
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(0, 0, 0, 128), 10.0f);

    ImGui::PopFont();
}
void RenderControlFrame(float x, float y, float w, float h)
{
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = ImVec2(x, y);

    // Rounded frame with white border (equivalent to NanoVG)
    draw_list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), g_ColorSimShip4, 10.0f);
    draw_list->AddRect(pos, ImVec2(pos.x + w, pos.y + h), g_ColorWhite, 10.0f, 0, 1.0f);

    // TWS text (kt) - Red
	ImGui::PushFont(g_FontArial08);
    char twsLabel[32];
    snprintf(twsLabel, sizeof(twsLabel), "TWS (kt)");
    draw_list->AddText(ImVec2(x + 5, y + 5), IM_COL32(255, 0, 0, 255), twsLabel);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial20);
    char windText[32];
    snprintf(windText, sizeof(windText), "%.0f", g_TWS_Kt);
    draw_list->AddText(ImVec2(x + 5, y + 15), IM_COL32(255, 0, 0, 255), windText);  // Rouge pour la valeur
    ImGui::PopFont();

    g_CtrlWind = vec4(x + 5, y + 15, 20, 20);

    // AWS text (kt) - Blue
    ImGui::PushFont(g_FontArial08);
    snprintf(windText, sizeof(windText), "AWS (kt)");
    draw_list->AddText(ImVec2(x + 5, y + h - 15), IM_COL32(0, 0, 255, 255), windText);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial20);
    snprintf(windText, sizeof(windText), "%.1f", ms_to_knot(g_Ship->AWS));
    draw_list->AddText(ImVec2(x + 5, y + h - 35), IM_COL32(0, 0, 255, 255), windText);  // Bleu pour la valeur
    ImGui::PopFont();
}
void RenderCompass(float x, float y, float radius)
{
    ImGui::PushFont(g_FontArial16);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 center = ImVec2(x, y);

    // Settings
    float thicknessCircle = 2.0f;
    float lengthGraduation = 5.0f;
    float sizeTriangle = 15.0f;
    float sizeTexDisplay = 8.0f;
    float sizeNumDisplay = 22.0f;
    float spacing = 18.0f;

    // Circle
    draw_list->AddCircleFilled(center, radius, g_ColorSimShip1, 0);

    // Graduations and cardinal points (every 10°)
    for (int i = 0; i < 360; i += 10)
    {
        float angle = i * M_PI / 180.0f;
        ImVec2 outer = ImVec2(x + cosf(angle) * radius, y + sinf(angle) * radius);
        ImVec2 inner = ImVec2(x + cosf(angle) * (radius - lengthGraduation), y + sinf(angle) * (radius - lengthGraduation));
        draw_list->AddLine(outer, inner, g_ColorWhite, 1.0f);
    }

    // HDG Triangle cyan
    float angleTriangle = (g_Ship->HDG - 90.0f) * M_PI / 180.0f;
    float baseTriangle = 10.0f;
    float heightTriangle = 17.0f;

    ImVec2 hdg_tip = ImVec2(x + cosf(angleTriangle) * radius, y + sinf(angleTriangle) * radius);
    ImVec2 hdg_left = ImVec2(
        x + cosf(angleTriangle) * (radius - heightTriangle) - sinf(angleTriangle) * (baseTriangle / 2),
        y + sinf(angleTriangle) * (radius - heightTriangle) + cosf(angleTriangle) * (baseTriangle / 2));
    ImVec2 hdg_right = ImVec2(
        x + cosf(angleTriangle) * (radius - heightTriangle) + sinf(angleTriangle) * (baseTriangle / 2),
        y + sinf(angleTriangle) * (radius - heightTriangle) - cosf(angleTriangle) * (baseTriangle / 2));

    ImVec2 hdg_pts[3] = { hdg_tip, hdg_left, hdg_right };
    draw_list->AddConvexPolyFilled(hdg_pts, 3, g_ColorCyan);

    // HDG display
    ImGui::PushFont(g_FontArial08);
    char strHDG[4] = "HDG";
	ImVec2 textPos = GetCenteredTextPos(strHDG, ImVec2(x, y - 2.0f * spacing - 3.0f), 0, 0, g_FontArial08);
    draw_list->AddText(textPos, g_ColorCyan, strHDG);
	ImGui::PopFont();

    ImGui::PushFont(g_FontArial20);
    string capStrHDG = display_geographic_angle(g_Ship->HDG, 1);
    textPos = GetCenteredTextPos(capStrHDG.c_str(), ImVec2(x, y - spacing - 5.0f), 0, 0, g_FontArial20);
    draw_list->AddText(textPos, g_ColorCyan, capStrHDG.c_str());
    ImGui::PopFont();

    // COG Triangle yellow
    angleTriangle = (g_Ship->COG - 90.0f) * M_PI / 180.0f;

    ImVec2 cog_tip = ImVec2(x + cosf(angleTriangle) * radius, y + sinf(angleTriangle) * radius);
    ImVec2 cog_left = ImVec2(
        x + cosf(angleTriangle) * (radius - heightTriangle) - sinf(angleTriangle) * (baseTriangle / 2),
        y + sinf(angleTriangle) * (radius - heightTriangle) + cosf(angleTriangle) * (baseTriangle / 2));
    ImVec2 cog_right = ImVec2(
        x + cosf(angleTriangle) * (radius - heightTriangle) + sinf(angleTriangle) * (baseTriangle / 2),
        y + sinf(angleTriangle) * (radius - heightTriangle) - cosf(angleTriangle) * (baseTriangle / 2));

    ImVec2 cog_pts[3] = { cog_tip, cog_left, cog_right };
    draw_list->AddConvexPolyFilled(cog_pts, 3, g_ColorYellow);

    // COG display
    ImGui::PushFont(g_FontArial08);
    char strCOG[4] = "COG";
	textPos = GetCenteredTextPos(strCOG, ImVec2(x, y - 5.0f), 0, 0, g_FontArial08);
    draw_list->AddText(textPos, g_ColorYellow, strCOG);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial20);
    string capStrCOG = display_geographic_angle(g_Ship->COG, 1);
    textPos = GetCenteredTextPos(capStrCOG.c_str(), ImVec2(x, y + spacing - 7.0f), 0, 0, g_FontArial20);
    draw_list->AddText(textPos, g_ColorYellow, capStrCOG.c_str());
    ImGui::PopFont();

    // Drift angle display
    ImGui::PushFont(g_FontArial12);
    char capStrDrift[16];
    snprintf(capStrDrift, sizeof(capStrDrift), "Drift: %.1f\xC2\xB0", g_Ship->DriftAngleDeg);
	textPos = GetCenteredTextPos(capStrDrift, ImVec2(x, y + 2.0f * spacing - 5.0f), 0, 0, g_FontArial12);
    draw_list->AddText(textPos, g_ColorAmbre, capStrDrift);
    ImGui::PopFont();

    // Wind marker red (triangle pointing outward)
    float angleWind = (g_TWS_Deg - 90.0f) * M_PI / 180.0f;
    float triangleHauteur = 10.8f;
    float triangleBase = 9.35f;
    float pointeR = radius - 3.0f;

    ImVec2 wind_tip = ImVec2(x + cosf(angleWind) * pointeR, y + sinf(angleWind) * pointeR);
    ImVec2 wind_base_center = ImVec2(x + cosf(angleWind) * (pointeR + triangleHauteur), y + sinf(angleWind) * (pointeR + triangleHauteur));
    float perpAngleL = angleWind + M_PI / 2.0f;
    float perpAngleR = angleWind - M_PI / 2.0f;

    ImVec2 wind_left = ImVec2(wind_base_center.x + cosf(perpAngleL) * (triangleBase / 2.0f), wind_base_center.y + sinf(perpAngleL) * (triangleBase / 2.0f));
    ImVec2 wind_right = ImVec2(wind_base_center.x + cosf(perpAngleR) * (triangleBase / 2.0f), wind_base_center.y + sinf(perpAngleR) * (triangleBase / 2.0f));

    ImVec2 wind_pts[3] = { wind_tip, wind_left, wind_right };
    draw_list->AddConvexPolyFilled(wind_pts, 3, g_ColorRed);

    // HDG instruction marker (colored rectangle)
    float angleInstruc = (g_Ship->HDGInstruction - 90.0f) * M_PI / 180.0f;
    float markDist = radius + 4.0f;
    float rectWidth = 6.0f;
    float rectHeight = 6.0f;

    ImVec2 centerMark = ImVec2(x + cosf(angleInstruc) * markDist, y + sinf(angleInstruc) * markDist);
    float dirX = cosf(angleInstruc);
    float dirY = sinf(angleInstruc);
    float perpX = cosf(angleInstruc + M_PI / 2);
    float perpY = sinf(angleInstruc + M_PI / 2);
    float halfWidth = rectWidth / 2;

    ImVec2 x1 = ImVec2(centerMark.x - perpX * halfWidth - dirX * (rectHeight / 2), centerMark.y - perpY * halfWidth - dirY * (rectHeight / 2));
    ImVec2 x2 = ImVec2(centerMark.x + perpX * halfWidth - dirX * (rectHeight / 2), centerMark.y + perpY * halfWidth - dirY * (rectHeight / 2));
    ImVec2 x3 = ImVec2(centerMark.x + perpX * halfWidth + dirX * (rectHeight / 2), centerMark.y + perpY * halfWidth + dirY * (rectHeight / 2));
    ImVec2 x4 = ImVec2(centerMark.x - perpX * halfWidth + dirX * (rectHeight / 2), centerMark.y - perpY * halfWidth + dirY * (rectHeight / 2));

    ImVec2 rect_pts[4] = { x1, x2, x3, x4 };
    draw_list->AddConvexPolyFilled(rect_pts, 4, g_Ship->bAutopilot ? g_ColorGreen : g_ColorRed);

    // White borders of the rectangle
    draw_list->AddLine(x1, x4, g_ColorWhite, 1.0f);
    draw_list->AddLine(x2, x3, g_ColorWhite, 1.0f);
    draw_list->AddLine(x3, x4, g_ColorWhite, 1.0f);

    // Rate of turn arc
    if (fabsf(g_Ship->YawVelocity * (180.0f / M_PI) * 60.0f) > 1.0f)
    {
        float rate = -g_Ship->YawVelocity * 60.0f;  // rad/min
        float rayonArc = radius - 10.0f;
        float angleStart = (g_Ship->HDG - 90.0f) * M_PI / 180.0f;
        float angleEnd = angleStart + rate;

        // Clamp the arc to avoid more than a full turn
        float arcSpan = angleEnd - angleStart;
        if (arcSpan > 2.0f * M_PI) arcSpan = 2.0f * M_PI;
        if (arcSpan < -2.0f * M_PI) arcSpan = -2.0f * M_PI;

        // Number of segments proportional to the arc
        int numSegments = (int)(fabsf(arcSpan) * 40.0f) + 2;
        if (numSegments > 128) numSegments = 128;

        std::vector<ImVec2> points;
        points.reserve(numSegments + 1);

        for (int i = 0; i <= numSegments; i++)
        {
            float t = (float)i / (float)numSegments;
            float angle = angleStart + t * arcSpan;
            points.push_back(ImVec2(
                x + cosf(angle) * rayonArc,
                y + sinf(angle) * rayonArc
            ));
        }

        draw_list->AddPolyline(points.data(), (int)points.size(), IM_COL32(0, 200, 200, 180), 0, 4.0f);
    }
    ImGui::PopFont();
}
void RenderBowThruster(float x, float y, float w, float h)
{
    if (!g_Ship->ship.HasBowThruster)
        return;

    ImGui::PushFont(g_FontArial16);
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    float largeur = w;
    float hauteur = h;
    float n = 2.0f * g_Ship->ship.BowThrusterStepMax;
    float separationLargeur = largeur / n;
    float valeur = 0.5f + g_Ship->BowThrusterCurrentStep / (2.0f * g_Ship->ship.BowThrusterStepMax);

    g_CtrlBowThruster = vec4(x, y, w, h);
    g_CtrlBowThrusterLeft = x;
    g_CtrlBowThrusterRight = x + w;

    // Slider background + border
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + largeur, y + hauteur), IM_COL32(36, 36, 36, 255));
    draw_list->AddRect(ImVec2(x, y), ImVec2(x + largeur, y + hauteur), g_ColorWhite, 0.0f, 0, 2.0f);

    // Green part (right)
    draw_list->AddRectFilled(ImVec2(x + largeur * 0.5f, y), ImVec2(x + largeur, y + hauteur), IM_COL32(0, 175, 0, 255));

    // BowThruster RPM positive (green)
    if (g_Ship->BowThrusterRpm > 0)
    {
        float r = g_Ship->BowThrusterRpm / g_Ship->ship.BowThrusterRpmMax;
        float xx = x + largeur * 0.5f + r * largeur * 0.5f;
        draw_list->AddRectFilled(ImVec2(x + largeur * 0.5f, y), ImVec2(xx, y + hauteur), IM_COL32(0, 255, 0, 255));
        draw_list->AddLine(ImVec2(xx, y), ImVec2(xx, y + hauteur), g_ColorWhite, 1.0f);
    }

    // Red part (left)
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + largeur * 0.5f, y + hauteur), IM_COL32(175, 0, 0, 255));

    // BowThruster RPM negative (red)
    if (g_Ship->BowThrusterRpm < 0)
    {
        float r = g_Ship->BowThrusterRpm / -g_Ship->ship.BowThrusterRpmMax;
        float xx = x + (1.0 - r) * largeur * 0.5f;
        draw_list->AddRectFilled(ImVec2(xx, y), ImVec2(x + largeur * 0.5f, y + hauteur), IM_COL32(255, 0, 0, 255));
        draw_list->AddLine(ImVec2(xx, y), ImVec2(xx, y + hauteur), g_ColorWhite, 1.0f);
    }

    // Separations
    for (int i = 1; i < n; i++)
    {
        float xPos = x + i * separationLargeur;
        draw_list->AddLine(ImVec2(xPos, y), ImVec2(xPos, y + hauteur), IM_COL32(0, 0, 0, 100), 1.0f);
    }

    // Cursor
    float curseurX = x + largeur * valeur;

    ImU32 gray = IM_COL32(64, 64, 64, 255);
    draw_list->AddRectFilled(ImVec2(curseurX, y - 2), ImVec2(curseurX + largeur / 2 - largeur * valeur, y + 2), gray);
    draw_list->AddRectFilled(ImVec2(curseurX, y + hauteur - 2), ImVec2(curseurX + largeur / 2 - largeur * valeur, y + hauteur + 2), gray);

    int ww = 6;
    draw_list->AddRectFilled(ImVec2(curseurX - ww / 2, y - 2), ImVec2(curseurX + ww / 2, y + hauteur + 2), IM_COL32(0, 0, 0, 255));

    ImGui::PopFont();
}
void RenderSternThruster(float x, float y, float w, float h)
{
    if (!g_Ship->ship.HasSternThruster)
        return;

    ImGui::PushFont(g_FontArial16);
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    float largeur = w;
    float hauteur = h;
    float n = 2.0f * g_Ship->ship.SternThrusterStepMax;
    float separationLargeur = largeur / n;
    float valeur = 0.5f + g_Ship->SternThrusterCurrentStep / (2.0f * g_Ship->ship.SternThrusterStepMax);

    g_CtrlSternThruster = vec4(x, y, w, h);
    g_CtrlSternThrusterLeft = x;
    g_CtrlSternThrusterRight = x + w;

    // Slider background + border
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + largeur, y + hauteur), IM_COL32(36, 36, 36, 255));
    draw_list->AddRect(ImVec2(x, y), ImVec2(x + largeur, y + hauteur), g_ColorWhite, 0.0f, 0, 2.0f);

    // Green part (right)
    draw_list->AddRectFilled(ImVec2(x + largeur * 0.5f, y), ImVec2(x + largeur, y + hauteur), IM_COL32(0, 175, 0, 255));

    // SternThruster RPM positive (green)
    if (g_Ship->SternThrusterRpm > 0)
    {
        float r = g_Ship->SternThrusterRpm / g_Ship->ship.SternThrusterRpmMax;
        float xx = x + largeur * 0.5f + r * largeur * 0.5f;
        draw_list->AddRectFilled(ImVec2(x + largeur * 0.5f, y), ImVec2(xx, y + hauteur), IM_COL32(0, 255, 0, 255));
        draw_list->AddLine(ImVec2(xx, y), ImVec2(xx, y + hauteur), g_ColorWhite, 1.0f);
    }

    // Red part (left)
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + largeur * 0.5f, y + hauteur), IM_COL32(175, 0, 0, 255));

    // SternThruster RPM negative (red)
    if (g_Ship->SternThrusterRpm < 0)
    {
        float r = g_Ship->SternThrusterRpm / -g_Ship->ship.SternThrusterRpmMax;
        float xx = x + (1.0 - r) * largeur * 0.5f;
        draw_list->AddRectFilled(ImVec2(xx, y), ImVec2(x + largeur * 0.5f, y + hauteur), IM_COL32(255, 0, 0, 255));
        draw_list->AddLine(ImVec2(xx, y), ImVec2(xx, y + hauteur), g_ColorWhite, 1.0f);
    }

    // Separations
    for (int i = 1; i < n; i++)
    {
        float xPos = x + i * separationLargeur;
        draw_list->AddLine(ImVec2(xPos, y), ImVec2(xPos, y + hauteur), IM_COL32(0, 0, 0, 100), 1.0f);
    }

    // Cursor
    float curseurX = x + largeur * valeur;

    ImU32 gray = IM_COL32(64, 64, 64, 255);
    draw_list->AddRectFilled(ImVec2(curseurX, y - 2), ImVec2(curseurX + largeur / 2 - largeur * valeur, y + 2), gray);
    draw_list->AddRectFilled(ImVec2(curseurX, y + hauteur - 2), ImVec2(curseurX + largeur / 2 - largeur * valeur, y + hauteur + 2), gray);

    int ww = 6;
    draw_list->AddRectFilled(ImVec2(curseurX - ww / 2, y - 2), ImVec2(curseurX + ww / 2, y + hauteur + 2), IM_COL32(0, 0, 0, 255));

    ImGui::PopFont();
}
void RenderSingleThrottle(ImDrawList* draw_list, float xx, float y, float w, float h, float dh, float n, float rpm, float rpmMax, float value)
{
    // Slider background + border
    draw_list->AddRectFilled(ImVec2(xx, y), ImVec2(xx + w, y + h), IM_COL32(36, 36, 36, 255));
    draw_list->AddRect(ImVec2(xx, y), ImVec2(xx + w, y + h), g_ColorWhite, 0.0f, 0, 2.0f);

    // Green part (upper)
    draw_list->AddRectFilled(ImVec2(xx, y), ImVec2(xx + w, y + h * 0.5f), IM_COL32(0, 175, 0, 255));

    // RPM positive (green)
    if (rpm > 0)
    {
        float r = rpm / rpmMax;
        float yy = y + (1.0f - r) * h * 0.5f;
        draw_list->AddRectFilled(ImVec2(xx, yy), ImVec2(xx + w, yy + r * h * 0.5f), IM_COL32(0, 255, 0, 255));
        draw_list->AddLine(ImVec2(xx, yy), ImVec2(xx + w, yy), g_ColorWhite, 1.0f);
    }

    // Red part (lower)
    draw_list->AddRectFilled(ImVec2(xx, y + h * 0.5f), ImVec2(xx + w, y + h), IM_COL32(175, 0, 0, 255));

    // RPM negative (red)
    if (rpm < 0)
    {
        float r = rpm / -rpmMax;
        float yy = y + h * 0.5f + r * h * 0.5f;
        draw_list->AddRectFilled(ImVec2(xx, y + h * 0.5f), ImVec2(xx + w, yy), IM_COL32(255, 0, 0, 255));
        draw_list->AddLine(ImVec2(xx, yy), ImVec2(xx + w, yy), g_ColorWhite, 1.0f);
    }

    // Separations
    for (int i = 1; i < n; i++)
    {
        float yPos = y + i * dh;
        draw_list->AddLine(ImVec2(xx, yPos), ImVec2(xx + w, yPos), IM_COL32(0, 0, 0, 100), 1.0f);
    }

    // Cursor
    float curseurY = y + h * (1 - value);

    // Lateral axes (gris)
    ImU32 gray = IM_COL32(64, 64, 64, 255);
    draw_list->AddRectFilled(ImVec2(xx - 3, curseurY), ImVec2(xx, curseurY + h / 2 - h * (1 - value)), gray);
    draw_list->AddRectFilled(ImVec2(xx + w, curseurY), ImVec2(xx + w + 3, curseurY + h / 2 - h * (1 - value)), gray);

    // Central controller (noir)
    int hh = 8;
    draw_list->AddRectFilled(ImVec2(xx - 2, curseurY - hh / 2), ImVec2(xx + w + 2, curseurY + hh / 2), IM_COL32(0, 0, 0, 255));

	// Thin line when controller is at idle position
    if (value == 0.5f)
        draw_list->AddLine(ImVec2(xx + 2.0f, y + h * 0.5f), ImVec2(xx + w - 2.0f, y + h * 0.5f), IM_COL32(255, 255, 255, 255), 1.0f);
}
void RenderLateralSpeed(ImDrawList* draw_list, float x, float xx, float w, float y, float h, bool bUpdate)
{
    static char text[50];
    ImGui::PushFont(g_FontArial16);

    // Bow lateral speed
    if (fabs(g_Ship->SOGbow) >= 0.01f)
    {
        if (bUpdate)
            sprintf_s(text, "%.2f kt", fabs(ms_to_knot(g_Ship->SOGbow)));

        if (g_Ship->SOGbow < 0)
        {
			ImVec2 textPos = GetCenteredTextPos(text, ImVec2(x - 35, y + 10), 0, 0, g_FontArial16);
            draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), text);
            // Red triangle (left)
            ImVec2 bow_pts[3] = {
                ImVec2(x - 13, y + 10),
                ImVec2(x - 5, y + 5),
                ImVec2(x - 5, y + 15)
            };
            draw_list->AddConvexPolyFilled(bow_pts, 3, IM_COL32(175, 0, 0, 255));
        }
        else
        {
			ImVec2 textPos = GetCenteredTextPos(text, ImVec2(xx + w + 35, y + 10), 0, 0, g_FontArial16);
            draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), text);
            // Green triangle (right)
            ImVec2 bow_pts[3] = {
                ImVec2(xx + w + 13, y + 10),
                ImVec2(xx + w + 5, y + 5),
                ImVec2(xx + w + 5, y + 15)
            };
            draw_list->AddConvexPolyFilled(bow_pts, 3, IM_COL32(0, 175, 0, 255));
        }
    }

    // Stern lateral speed
    if (fabs(g_Ship->SOGstern) >= 0.01f)
    {
        if (bUpdate)
            sprintf_s(text, "%.2f kt", fabs(ms_to_knot(g_Ship->SOGstern)));

        if (g_Ship->SOGstern < 0)
        {
			ImVec2 textPos = GetCenteredTextPos(text, ImVec2(x - 35, y + h - 10), 0, 0, g_FontArial16);
            draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), text);
            // Red triangle (left)
            ImVec2 stern_pts[3] = {
                ImVec2(x - 13, y + h - 10),
                ImVec2(x - 5, y + h - 15),
                ImVec2(x - 5, y + h - 5)
            };
            draw_list->AddConvexPolyFilled(stern_pts, 3, IM_COL32(175, 0, 0, 255));
        }
        else
        {
			ImVec2 textPos = GetCenteredTextPos(text, ImVec2(xx + w + 35, y + h - 10), 0, 0, g_FontArial16);
            draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), text);
            // Green triangle (right)
            ImVec2 stern_pts[3] = {
                ImVec2(xx + w + 13, y + h - 10),
                ImVec2(xx + w + 5, y + h - 15),
                ImVec2(xx + w + 5, y + h - 5)
            };
            draw_list->AddConvexPolyFilled(stern_pts, 3, IM_COL32(0, 175, 0, 255));
        }
    }
    ImGui::PopFont();
}
void RenderThrottle(float x, float y, float w, float h, float e)
{
    ImGui::PushFont(g_FontArial20);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    float n = std::min(10.0f, 2.0f * g_Ship->ship.PowerStepMax);
    float dh = h / n;
    float value1 = 0.5f + g_Ship->PowerCurrentStep1 / (2.0f * g_Ship->ship.PowerStepMax);
    float value2 = 0.5f + g_Ship->PowerCurrentStep2 / (2.0f * g_Ship->ship.PowerStepMax);
    float xx = x;

    // Speed Update
    static double prevTime = 0.0;
    bool bUpdate = false;
    if (g_TimerScene.getTime() - prevTime > 0.5)
    {
        bUpdate = true;
        prevTime = g_TimerScene.getTime();
    }
    static char text[50];
    if (bUpdate)
        sprintf_s(text, "%.2f kt", fabs(ms_to_knot(g_Ship->SOG)));

    if (g_Ship->ship.nPropeller == 2)
    {
        g_CtrlThrottle1 = vec4(x, y, w, h);
        g_CtrlThrottleHigh1 = y;
        g_CtrlThrottleLow1 = y + h;

        g_CtrlThrottle2 = vec4(x + w + e, y, w, h);
        g_CtrlThrottleHigh2 = y;
        g_CtrlThrottleLow2 = y + h;

        g_CtrlThrottle12 = vec4(x + w, y, e, h);
        g_CtrlThrottleHigh12 = y;
        g_CtrlThrottleLow12 = y + h;

        // SLIDER 1
        RenderSingleThrottle(draw_list, xx, y, w, h, dh, n, g_Ship->PropRpm1, g_Ship->ship.PropRpmMax, value1);

        // SLIDER 2
        xx = x + w + e;
        RenderSingleThrottle(draw_list, xx, y, w, h, dh, n, g_Ship->PropRpm2, g_Ship->ship.PropRpmMax, value2);
    }
    else
    {
        g_CtrlThrottle1 = vec4(x, y, w, h);
        g_CtrlThrottle2 = vec4(0.0f);
        g_CtrlThrottle12 = vec4(0.0f);
        g_CtrlThrottleHigh1 = y;
        g_CtrlThrottleLow1 = y + h;

        RenderSingleThrottle(draw_list, xx, y, w, h, dh, n, g_Ship->PropRpm1, g_Ship->ship.PropRpmMax, value1);
    }

    // TEXT Speed
	ImVec2 textPos = GetCenteredTextPos(text, ImVec2(xx + w + 35, y + h * 0.5f), 0, 0, g_FontArial20);
    draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), text);

    // Directional triangles
    float triangleX = xx + w + 35;

    // Upward triangle (forward)
    ImVec2 tri_fwd_pts[3] = {
        ImVec2(triangleX - 10, y + h * 0.5f - 12),
        ImVec2(triangleX + 10, y + h * 0.5f - 12),
        ImVec2(triangleX, y + h * 0.5f - 28)
    };
    draw_list->AddConvexPolyFilled(tri_fwd_pts, 3, g_Ship->LinearVelocity > 0 ? g_ColorWhite : IM_COL32(120, 120, 120, 255));

    // Downward triangle (backward)
    ImVec2 tri_bwd_pts[3] = {
        ImVec2(triangleX - 10, y + h * 0.5f + 12),
        ImVec2(triangleX + 10, y + h * 0.5f + 12),
        ImVec2(triangleX, y + h * 0.5f + 28)
    };
    draw_list->AddConvexPolyFilled(tri_bwd_pts, 3, g_Ship->LinearVelocity < 0 ? g_ColorWhite : IM_COL32(120, 120, 120, 255));

    // Lateral speeds (bow/stern)
    RenderLateralSpeed(draw_list, x, xx, w, y, h, bUpdate);

    ImGui::PopFont();
}
void RenderRudder(float x, float y, float w, float h)
{
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Left part (red)
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w / 2, y + h), IM_COL32(175, 0, 0, 255));
    
    // Right part (green)
    draw_list->AddRectFilled(ImVec2(x + w / 2, y), ImVec2(x + w, y + h), IM_COL32(0, 175, 0, 255));

    g_CtrlRudder = vec4(x, y, w, h);
    g_CtrlRudderLeft = x;
    g_CtrlRudderRight = x + w;

    // Graduations
    int nbGraduations = static_cast<int>(ceil(g_Ship->ship.RudderStepMax / 10.0f));
    ImU32 gradColor = IM_COL32(150, 150, 150, 255);

    for (int i = 1; i < nbGraduations; i++)
    {
        float graduationValue = i * 10.0f;
        float ratio = std::min(graduationValue / g_Ship->ship.RudderStepMax, 1.0f);

        // Right graduations
        float posX = x + w / 2 + ratio * (w / 2);
        draw_list->AddLine(ImVec2(posX, y), ImVec2(posX, y + h), gradColor, 1.0f);

        // Left graduations
        posX = x + w / 2 - ratio * (w / 2);
        draw_list->AddLine(ImVec2(posX, y), ImVec2(posX, y + h), gradColor, 1.0f);
    }

    // Left border
    draw_list->AddRect(ImVec2(x, y), ImVec2(x + w / 2, y + h), g_ColorWhite, 0.0f, 0, 1.0f);
    
    // Right border
    draw_list->AddRect(ImVec2(x + w / 2, y), ImVec2(x + w, y + h), g_ColorWhite, 0.0f, 0, 1.0f);

    // Actual rudder position
    float barrePos0 = x + w / 2 + ((float)-g_Ship->RudderAngleDeg / (float)g_Ship->ship.RudderStepMax) * (w / 2);
    ImU32 rudderColor = g_Ship->RudderAngleDeg < 0.0f ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
    draw_list->AddRectFilled(ImVec2(barrePos0, y + 1), ImVec2(x + w / 2, y + h - 1), rudderColor);

    // Indicator bar
    if (!g_Ship->bAutopilot)
    {
        float barrePos1 = x + w / 2 + ((float)-g_Ship->RudderCurrentStep / (float)g_Ship->ship.RudderStepMax) * (w / 2);
        float barreWidth = 3.0f;
        draw_list->AddRectFilled(ImVec2(barrePos1 - barreWidth / 2, y), ImVec2(barrePos1 + barreWidth / 2, y + h), g_ColorBlack);

        // Small horizontal ticks when RudderCurrentStep == 0
        if (g_Ship->RudderCurrentStep == 0.0f)
        {
            float tickWidth = w * 0.05f;
            float tickHeight = 2.0f;
            float centerX = x + w / 2;
            float tickY = y + h / 2;

            // Left tick
            draw_list->AddRectFilled(ImVec2(centerX - tickWidth - 3.0f, tickY - tickHeight / 2), ImVec2(centerX - 3.0f, tickY + tickHeight / 2), g_ColorBlack);

            // Right tick
            draw_list->AddRectFilled(ImVec2(centerX + 3.0f, tickY - tickHeight / 2), ImVec2(centerX + tickWidth + 3.0f, tickY + tickHeight / 2), g_ColorBlack);
        }
    }
    else
    {
        float barrePos1 = x + w / 2 + ((float)-g_Ship->RudderTargetDeg / (float)g_Ship->ship.RudderStepMax) * (w / 2);
        float barreWidth = 1.0f;
        draw_list->AddRectFilled(ImVec2(barrePos1 - barreWidth / 2, y), ImVec2(barrePos1 + barreWidth / 2, y + h), g_ColorWhite);
    
        // Small horizontal ticks when RudderTargetDeg == 0
        if (g_Ship->RudderTargetDeg == 0.0f)
        {
            float tickWidth = w * 0.05f;
            float tickHeight = 2.0f;
            float centerX = x + w / 2;
            float tickY = y + h / 2;

            // Left tick
            draw_list->AddRectFilled(ImVec2(centerX - tickWidth - 3.0f, tickY - tickHeight / 2), ImVec2(centerX - 3.0f, tickY + tickHeight / 2), g_ColorWhite);

            // Right tick
            draw_list->AddRectFilled(ImVec2(centerX + 3.0f, tickY - tickHeight / 2), ImVec2(centerX + tickWidth + 3.0f, tickY + tickHeight / 2), g_ColorWhite);
        }
    }
}
void RenderTime(float x, float y, float w, float h)
{
    ImGui::PushFont(g_FontArial20);

    sHM hm = g_Sky->GetTime();
    char buffer[12];
    int hr = hm.hour + hm.timezoneOffsetHours;
    if (hr > 23)
        hr -= 24;
    snprintf(buffer, sizeof(buffer), "%02d:%02d", hr, hm.minute);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Time in HH:MM format
    draw_list->AddText(ImVec2(x, y - 3), IM_COL32(0, 0, 0, 255), buffer);

    // Control zones for hours and minutes
	g_CtrlTimeHour = vec4(x, y - 2, 23, 20);            // Use wheelmouse on this area to change hours
	g_CtrlTimeMinute = vec4(x + 24, y - 2, 23, 20);     // Use wheelmouse on this area to change minutes
	g_CtrlNow = vec4(x, y - 2, 47, 20);                 // Use mouse click on this area to set time to now

	// Anchor icon
	VkDescriptorSet* tex_anchor = (VkDescriptorSet*)g_ImgAnchor->GetImGuiDescriptorSet();
	ImVec2 icon_pos0 = ImVec2(x + 53, y - 3);
    draw_list->AddRectFilled(icon_pos0, ImVec2(icon_pos0.x + 20, icon_pos0.y + 20), g_ColorSimShip1, 5.0f); // Background as in RenderEnv
    draw_list->AddImage((ImTextureID)tex_anchor, icon_pos0, ImVec2(icon_pos0.x + 20, icon_pos0.y + 20));
    g_CtrlAnchor = vec4(x + 53, y - 3, 20.0f, 20.0f);

    // Bollard icon
    VkDescriptorSet* tex_bollard = (VkDescriptorSet*)g_ImgBollard->GetImGuiDescriptorSet(); 
    ImVec2 icon_pos1 = ImVec2(x + w - 23, y - 3);
    draw_list->AddRectFilled(icon_pos1, ImVec2(icon_pos1.x + 20, icon_pos1.y + 20), g_ColorSimShip1, 5.0f); // Background as in RenderEnv
    draw_list->AddImage((ImTextureID)tex_bollard, icon_pos1, ImVec2(icon_pos1.x + 20, icon_pos1.y + 20));
    g_CtrlBollard = vec4(x + w - 23, y - 3, 20.0f, 20.0f);

    ImGui::PopFont();
}
void RenderPitchRollRot(float x, float y, float w, float h)
{
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Black frame arrondi
    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), g_ColorSimShip1, 5.0f);

    float sizeTitle = 8.0f;
    float sizeValue = 12.0f;
    float x1 = x + 40;
    char text[50];
    float y1 = y + 12;

    // PITCH
    ImGui::PushFont(g_FontArial10);
    strcpy_s(text, "PITCH");
	ImVec2 textPos = GetLeftAlignedTextPos(text, ImVec2(x + 5, y1), 0, 0, g_FontArial10);
    draw_list->AddText(textPos, g_ColorWhite, text);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial14);
    sprintf_s(text, "%.1f\xC2\xB0", glm::degrees(g_Ship->Pitch));
	textPos = GetLeftAlignedTextPos(text, ImVec2(x1, y1), 0, 0, g_FontArial14);
    draw_list->AddText(textPos, g_ColorCyan, text);
    ImGui::PopFont();

    // ROLL
    ImGui::PushFont(g_FontArial10);
    y1 += sizeValue + 6;
    strcpy_s(text, "ROLL");
	textPos = GetLeftAlignedTextPos(text, ImVec2(x + 5, y1), 0, 0, g_FontArial10);
    draw_list->AddText(textPos, g_ColorWhite, text);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial14);
    sprintf_s(text, "%.1f\xC2\xB0", glm::degrees(g_Ship->Roll));
	textPos = GetLeftAlignedTextPos(text, ImVec2(x1, y1), 0, 0, g_FontArial14);
    draw_list->AddText(textPos, g_ColorCyan, text);
    ImGui::PopFont();

    // ROT (Rate of Turn)
    ImGui::PushFont(g_FontArial10);
    y1 += sizeValue + 6;
    strcpy_s(text, "ROT");
	textPos = GetLeftAlignedTextPos(text, ImVec2(x + 5, y1), 0, 0, g_FontArial10);
    draw_list->AddText(textPos, g_ColorWhite, text);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial14);
    sprintf_s(text, "%.0f\xC2\xB0/mn", -g_Ship->YawVelocity * (180.0f / M_PI) * 60.0f);
	textPos = GetLeftAlignedTextPos(text, ImVec2(x1, y1), 0, 0, g_FontArial14);
    draw_list->AddText(textPos, g_ColorCyan, text);
    ImGui::PopFont();

    // DIAMETER
    ImGui::PushFont(g_FontArial10);
    y1 += sizeValue + 6;
    strcpy_s(text, "DIAM");
	textPos = GetLeftAlignedTextPos(text, ImVec2(x + 5, y1), 0, 0, g_FontArial10);
    draw_list->AddText(textPos, g_ColorWhite, text);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial14);
    if (g_Ship->TurnDiameter_L < 20.0f)
        sprintf_s(text, "%.0f m", g_Ship->TurnDiameter_m);
    else
        sprintf_s(text, "> %.0f m", 20.0f * g_Ship->ship.Length);
	textPos = GetLeftAlignedTextPos(text, ImVec2(x1, y1), 0, 0, g_FontArial14);
    draw_list->AddText(textPos, g_ColorCyan, text);
    ImGui::PopFont();

    // RADIUS
    ImGui::PushFont(g_FontArial10);
    y1 += sizeValue + 6;
    strcpy_s(text, "DIAM");
	textPos = GetLeftAlignedTextPos(text, ImVec2(x + 5, y1), 0, 0, g_FontArial10);
    draw_list->AddText(textPos, g_ColorWhite, text);
    ImGui::PopFont();

    ImGui::PushFont(g_FontArial14);
    if (g_Ship->TurnDiameter_L < 20.0f)
        sprintf_s(text, "%.1f L", g_Ship->TurnDiameter_L);
    else
        sprintf_s(text, "> 20 L");
	textPos = GetLeftAlignedTextPos(text, ImVec2(x1, y1), 0, 0, g_FontArial14);
    draw_list->AddText(textPos, g_ColorCyan, text);
    ImGui::PopFont();
}
void RenderControlFrameNight(float x, float y, float w, float h)
{
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Night overlay (same as RenderEnv)
    if (g_Sky->SunPosition.y < 0.0f)
        draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 128), 10.0f);
}
void RenderAutopilot(float x, float y)
{
    ImGui::PushFont(g_FontArial16);

    float w = 80.0f;
    float h = 136.0f;
    bool isAuto = g_Ship->bAutopilot;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = ImVec2(x, y);

    // Main frame + top buttons (unchanged)
    draw_list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), g_ColorSimShip4, 10.0f);
    draw_list->AddRect(pos, ImVec2(pos.x + w, pos.y + h), g_ColorWhite, 10.0f, 0, 1.0f);

    // Auto/Stby button (equivalent DrawRoundRect)
    float buttonWidth = 72;
    float buttonHeight = 28;
    ImU32 color = isAuto ? g_ColorGreen : g_ColorRed;
    ImVec2 btn_pos = ImVec2(x + (w - buttonWidth) / 2, y + 5);
    draw_list->AddRectFilled(btn_pos, ImVec2(btn_pos.x + buttonWidth, btn_pos.y + buttonHeight), color, 5.0f);
    const char* autoText = isAuto ? "AUTO" : "STBY";
	ImVec2 texPos = GetCenteredTextPos(autoText, ImVec2(btn_pos.x + buttonWidth / 2, btn_pos.y + buttonHeight / 2), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorWhite, autoText);
    g_CtrlAutopilotCMD = vec4(x + (w - buttonWidth) / 2, y + 5, buttonWidth, buttonHeight);

    // Course instruction
    buttonWidth = 60;
    buttonHeight = 18;
    string courseStr = display_geographic_angle(g_Ship->HDGInstruction, 0);
    ImVec2 course_pos = ImVec2(x + (w - buttonWidth) / 2, y + 38);
    draw_list->AddRectFilled(course_pos, ImVec2(course_pos.x + buttonWidth, course_pos.y + buttonHeight), g_ColorSimShip1, 3.0f);
	texPos = GetCenteredTextPos(courseStr.c_str(), ImVec2(course_pos.x + buttonWidth / 2, course_pos.y + buttonHeight / 2), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorCyan, courseStr.c_str());

    // Control buttons - NANO VG POSITIONS (top-left corner → ImGui center)
    float buttonSize = 30;
    float radius = buttonSize * 0.5f;

    // ADJUSTMENT: + radius in X and Y to compensate for ImGui center
    float buttonX1 = x + 7 + radius;  // ← +15px X
    float buttonX2 = x + 43 + radius;  // ← +15px X  
    float buttonY1 = y + 65 + radius;  // ← +15px Y
    float buttonY2 = y + 100 + radius; // ← +15px Y

    // Button <
    draw_list->AddCircleFilled(ImVec2(buttonX1, buttonY1), radius, g_ColorSimShip3, 32);
	texPos = GetCenteredTextPos("<", ImVec2(buttonX1, buttonY1), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorWhite, "<");

    // Button >
    draw_list->AddCircleFilled(ImVec2(buttonX2, buttonY1), radius, g_ColorSimShip3, 32);
    texPos = GetCenteredTextPos(">", ImVec2(buttonX2, buttonY1), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorWhite, ">");

    // Button <<
    draw_list->AddCircleFilled(ImVec2(buttonX1, buttonY2), radius, g_ColorSimShip3, 32);
    texPos = GetCenteredTextPos("<<", ImVec2(buttonX1, buttonY2), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorWhite, "<<");

    // Button >>
    draw_list->AddCircleFilled(ImVec2(buttonX2, buttonY2), radius, g_ColorSimShip3, 32);
    texPos = GetCenteredTextPos(">>", ImVec2(buttonX2, buttonY2), 0.0f, 0.0f, g_FontArial16);
    draw_list->AddText(texPos, g_ColorWhite, ">>");

    // Control zones (centered on circles)
    g_CtrlAutopilotM1 = vec4(buttonX1 - radius, buttonY1 - radius, buttonSize, buttonSize);
    g_CtrlAutopilotP1 = vec4(buttonX2 - radius, buttonY1 - radius, buttonSize, buttonSize);
    g_CtrlAutopilotM10 = vec4(buttonX1 - radius, buttonY2 - radius, buttonSize, buttonSize);
    g_CtrlAutopilotP10 = vec4(buttonX2 - radius, buttonY2 - radius, buttonSize, buttonSize);

    // Night overlay
    if (g_Sky->SunPosition.y < 0.0f)
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(0, 0, 0, 128), 10.0f);

    ImGui::PopFont();
}
void RenderChronometer(float x, float y)
{
    if (!g_bChrono)
        return;

    ImGui::PushFont(g_FontArial20);

    double secondsTotal = g_Chrono.getTime();
    int totalSec = static_cast<int>(secondsTotal);
    int hours = totalSec / 3600;
    int minutes = (totalSec % 3600) / 60;
    int seconds = totalSec % 60;
    int deciseconds = static_cast<int>((secondsTotal - totalSec) * 10);

    ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << hours << ":"
        << std::setw(2) << std::setfill('0') << minutes << ":"
        << std::setw(2) << std::setfill('0') << seconds << "."
        << std::setw(0) << std::setfill('0') << deciseconds;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    
    ImVec2 textPos = GetCenteredTextPos(oss.str().c_str(), ImVec2(x, y), 0, 0, g_FontArial20);
    ImVec2 textSize = ImGui::CalcTextSize(oss.str().c_str());

    // Margins for the rectangle (padding)
    float paddingX = 20.0f;
    float paddingY = 10.0f;

    // Position and size of the rectangle
    ImVec2 rectMin = ImVec2(textPos.x - paddingX, textPos.y - paddingY);
    ImVec2 rectMax = ImVec2(textPos.x + textSize.x + paddingX, textPos.y + textSize.y + paddingY);

    // White rectangle, almost opaque (alpha 230/255 ≈ 0.9)
    draw_list->AddRectFilled(rectMin, rectMax, IM_COL32(255, 255, 255, 230), 8.0f);

    // Optional border (thin black)
    draw_list->AddRect(rectMin, rectMax, IM_COL32(0, 0, 0, 120), 8.0f, 0, 1.0f);

    draw_list->AddText(textPos, IM_COL32(0, 0, 0, 255), oss.str().c_str());

    ImGui::PopFont();
}
void RenderDashboard()
{
    // Ship controls interface
    if (g_Ship && g_Ship->bVisible && !g_bBinoculars)
    {
        int widthAutopilot = 80;
        int widthSeparation = 5;
        float xChrono;

        if (g_Ship->ship.HasBowThruster)
        {
            int widthControlFrame = 455;
            if (g_Ship->ship.nPropeller == 2)
                widthControlFrame += 30 + 10;
            int startX = g_WindowW_2 - (widthControlFrame + 2 * widthSeparation + 2 * widthAutopilot) / 2;
            g_CtrlPanel = vec4(startX, 2, startX + widthControlFrame + 2 * widthSeparation + 2 * widthAutopilot, 136);
            RenderEnv(startX, 2);
            int xFrame = startX + widthAutopilot + widthSeparation;
            RenderControlFrame(xFrame, 2, widthControlFrame, 136);
            int x = xFrame + 90;
            RenderCompass(x, 70, 58);
            x += 80;
            RenderBowThruster(x, 35, 50, 20);
            RenderSternThruster(x, 65, 50, 20);
            x += 70;
            RenderThrottle(x, 10, 30, 100, 10);
            xChrono = x;
            x -= 45;
            if (g_Ship->ship.nPropeller == 2)
                x += (30 + 10) / 2;
            RenderRudder(x, 115, 120, 15);
            x += 150;
            if (g_Ship->ship.nPropeller == 2)
                x += (30 + 10) / 2;
            RenderTime(x, 13, 100, 30);
            RenderPitchRollRot(x, 35, 100, 95);
            RenderControlFrameNight(xFrame, 2, widthControlFrame, 136);
            RenderAutopilot(xFrame + widthControlFrame + widthSeparation, 2);
        }
        else
        {
            int widthControlFrame = 455 - 35;
            if (g_Ship->ship.nPropeller == 2)
                widthControlFrame += 30 + 10;
            int startX = g_WindowW_2 - (widthControlFrame + 2 * widthSeparation + 2 * widthAutopilot) / 2;
            g_CtrlPanel = vec4(startX, 2, startX + widthControlFrame + 2 * widthSeparation + 2 * widthAutopilot, 136);
            RenderEnv(startX, 2);
            int xFrame = startX + widthAutopilot + widthSeparation;
            RenderControlFrame(xFrame, 2, widthControlFrame, 136);
            int x = xFrame + 90;
            RenderCompass(x, 70, 58);
            x += 110;
            RenderThrottle(x, 10, 30, 100, 10);
            xChrono = x;
            x -= 45;
            if (g_Ship->ship.nPropeller == 2)
                x += (30 + 10) / 2;
            RenderRudder(x, 115, 120, 15);
            x += 150;
            if (g_Ship->ship.nPropeller == 2)
                x += (30 + 10) / 2;
            RenderTime(x, 13, 100, 30);
            RenderPitchRollRot(x, 35, 100, 95);
            RenderControlFrameNight(xFrame, 2, widthControlFrame, 136);
            RenderAutopilot(xFrame + widthControlFrame + widthSeparation, 2);
        }
        RenderChronometer(g_WindowW_2, 160.0f);
    }
}
void RenderStatusbar()
{
    if (!g_bShowStatusBar)
        return;

    ImGui::PushFont(g_FontArial16);

    char text[256];
    vec3 position = g_Camera.GetPosition();
    float distCameraShip = glm::length(g_Ship->ship.Position - position);
    vec2 lonlat = opengl_to_lonlat(position.x, position.z);
    sprintf_s(text, "CAMERA x = %.2f y = %.2f z = %.2f Heading = %03d\xc2\xb0 Roll = %d\xc2\xb0 Pitch = %03d\xc2\xb0       Distance to ship = %d m      Lon = %.6f Lat =  %.6f",
        position.x, position.y, position.z,
        (int)g_Camera.GetNorthAngleDEG(), (int)g_Camera.GetAttitudeDEG(), (int)g_Camera.GetRollDEG(),
        int(distCameraShip), lonlat.x, lonlat.y);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // White semi-transparent background rectangle (at the bottom of the screen)
    draw_list->AddRectFilled(ImVec2(0, g_WindowH - 20), ImVec2(g_WindowW, g_WindowH), IM_COL32(255, 255, 255, 64));

    // Black text vertically centered
    ImVec2 box_left = ImVec2(0, g_WindowH - 20 + 10); 
    ImVec2 text_pos = GetLeftAlignedTextPos(text, box_left, 5.0f, 0.0f, g_FontArial16);

    draw_list->AddText(text_pos, IM_COL32(0, 0, 0, 255), text);

    ImGui::PopFont();
}
void RenderTitle()
{
    float x = g_WindowW - 55.0f;
    float y = g_WindowH - 45.0f;
    
    ImGui::PushFont(g_FontCaveat36);  // "Caveat" font equivalent

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    const char* text = "SimShip";

    // Adjust position according to alignment (simulates nvgTextAlign)
    ImVec2 text_size = ImGui::CalcTextSize(text);
    float textX = x;

    textX -= text_size.x * 0.5f;

    // White title
    draw_list->AddText(ImVec2(textX, y), g_ColorWhite, text);

    ImGui::PopFont();
}
void RenderInfo()
{
    ImGui::PushFont(g_FontArial36);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Screenshot name
    if (g_CaptureName.length() > 0 && g_CaptureDisplayTime > 0.0f)
    {
        std::string s(g_CaptureName.begin(), g_CaptureName.end());

        // Get viewport (window size)
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 window_center = viewport->GetCenter();

        // Calculate text size for precise offset
        ImVec2 text_size = ImGui::CalcTextSize(s.c_str());

        // Final position: center - (text size / 2)
        ImVec2 text_pos = ImVec2(window_center.x - text_size.x * 0.5f, window_center.y - text_size.y * 0.5f);

        // Transparent black rectangle (padding 10px around text)
        ImVec2 rect_min = ImVec2(text_pos.x - 10.0f, text_pos.y - 5.0f);
        ImVec2 rect_max = ImVec2(text_pos.x + text_size.x + 10.0f, text_pos.y + text_size.y + 5.0f);

        // Black color 70% transparent
        draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(0, 0, 0, 180), 4.0f);

        // White text on top
        draw_list->AddText(text_pos, g_ColorWhite, s.c_str());

        g_CaptureDisplayTime -= ImGui::GetIO().DeltaTime;
    }
    ImGui::PopFont();
}
void RenderShortcuts()
{
    if (!g_bShowShortcuts)
        return;

    ImGui::PushFont(g_FontArial20);

    // Split : from "W" to "O" → left col
    size_t splitIndex = 24;

    float padding = 20.0f;
    float lineHeight = 22.0f;
    float boxWidth = 560.0f;
    float boxHeight = lineHeight * std::max(22, (int)(vShortcuts.size() - 22)) + padding * 2;

    // Center the box
    float x = (g_WindowW - boxWidth) / 2.0f;
    float y = (g_WindowH - boxHeight) / 2.0f;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = ImVec2(x, y);

    // Semi-transparent background
    draw_list->AddRectFilled(pos, ImVec2(x + boxWidth, y + boxHeight), IM_COL32(0, 0, 0, 160));

    // Central dividing line
    float separatorX = x + padding + (boxWidth - 2 * padding) * 0.5f;
    draw_list->AddLine(ImVec2(separatorX, y + padding),
        ImVec2(separatorX, y + boxHeight - padding),
        IM_COL32(255, 255, 255, 180), 2.0f);

    // Width of a column
    float columnWidth = (boxWidth - 2 * padding) * 0.5f;

    // Column positions
    float colLeftXKey = x + padding;
    float colLeftXDesc = x + padding + columnWidth - 20;
    float colRightXKey = x + padding + columnWidth + 20;
    float colRightXDesc = x + boxWidth - padding;

    // Left column
    float textYLeft = y + padding + lineHeight * 0.8f;
    for (size_t i = 0; i <= splitIndex && i < vShortcuts.size(); ++i)
    {
        // Key (left-aligned)
        draw_list->AddText(ImVec2(colLeftXKey, textYLeft), g_ColorWhite, vShortcuts[i].first.c_str());
        // Description (right-aligned)
        float descWidth = ImGui::CalcTextSize(vShortcuts[i].second.c_str()).x;
        draw_list->AddText(ImVec2(colLeftXDesc - descWidth, textYLeft), g_ColorWhite, vShortcuts[i].second.c_str());
        textYLeft += lineHeight;
    }

    // Right column
    float textYRight = y + padding + lineHeight * 0.8f;
    for (size_t i = splitIndex + 1; i < vShortcuts.size(); ++i)
    {
        // Key (left-aligned)
        draw_list->AddText(ImVec2(colRightXKey, textYRight), g_ColorWhite, vShortcuts[i].first.c_str());
        // Description (right-aligned)
        float descWidth = ImGui::CalcTextSize(vShortcuts[i].second.c_str()).x;
        draw_list->AddText(ImVec2(colRightXDesc - descWidth, textYRight), g_ColorWhite, vShortcuts[i].second.c_str());
        textYRight += lineHeight;
    }

    ImGui::PopFont();
}
void RenderCompassOfBinoculars()
{
    float aspect = float(g_WindowW) / float(g_WindowH);
    constexpr float fovY_rad(glm::radians(45.0f));
    float fovX_rad = 2.0f * atanf(aspect * tanf(fovY_rad * 0.5f));
    float degVisibleHorizontal = glm::degrees(fovX_rad);

    float capCenter = g_Camera.GetNorthAngleDEG();
    float centerX = float(g_WindowW) * 0.5f;
    float rulerY = float(g_WindowH) * 0.5f;

    float visibleRatio = 0.7f;
    float halfVisiblePixels = (float(g_WindowW) * visibleRatio) * 0.5f;
    float leftBound = centerX - halfVisiblePixels;
    float rightBound = centerX + halfVisiblePixels;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // --- Heading label metrics (must be computed first to size the gap) ---
	string headingText = display_geographic_angle(capCenter, 1);

    ImGui::PushFont(g_FontArial20);  // larger font for the main heading label
    ImVec2 capTextSize = ImGui::CalcTextSize(headingText.c_str());
    float  capPadding = 6.f;
    float  capOffsetY = 30.f;     // upward offset from the ruler
    float  gapHalfHeight = capTextSize.y * 0.5f + capPadding;

    float capTextX = centerX - capTextSize.x * 0.5f;
    float capTextY = rulerY - capTextSize.y * 0.5f - capOffsetY;

    // Gap centre is shifted up by the same offset
    float gapCenterY = rulerY - capOffsetY;
    float gapTop = gapCenterY - gapHalfHeight;
    float gapBottom = gapCenterY + gapHalfHeight;

    // --- Horizontal ruler (limited to visibleRatio) ---
    draw_list->AddLine(ImVec2(leftBound, rulerY), ImVec2(rightBound, rulerY), g_ColorWhite, 2.0f);
    ImGui::PopFont();

    // --- Graduations ---
    ImGui::PushFont(g_FontArial16);
    int minDegree = int(capCenter - degVisibleHorizontal * 0.5f);
    int maxDegree = int(capCenter + degVisibleHorizontal * 0.5f);

    for (int deg = minDegree; deg <= maxDegree; ++deg)
    {
        int displayedDeg = deg;
        if (displayedDeg < 0)    displayedDeg += 360;
        if (displayedDeg >= 360) displayedDeg -= 360;

        float i = float(deg) - capCenter;
        float x = centerX + (i * float(g_WindowW) / degVisibleHorizontal);

        if (x < leftBound || x > rightBound)
            continue;

        bool  isTenDegree = (displayedDeg % 10 == 0);
        float gradHeight = isTenDegree ? 22.f : 12.f;

        draw_list->AddLine(ImVec2(x, rulerY - gradHeight * 0.5f), ImVec2(x, rulerY + gradHeight * 0.5f), IM_COL32(200, 200, 220, 255), 1.0f);

        if (isTenDegree)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "%03d", displayedDeg);
            draw_list->AddText(ImVec2(x, rulerY + gradHeight * 0.5f + 4.f), IM_COL32(220, 220, 255, 255), buf);
        }
    }
    ImGui::PopFont();

    // --- Central vertical line with a gap where the label sits ---
    // Upper segment (sky side)
    draw_list->AddLine(ImVec2(centerX, rulerY - 200.f), ImVec2(centerX, gapTop), g_ColorWhite, 2.0f);

    // Lower segment (ground side) — runs from gap down through the ruler
    draw_list->AddLine(ImVec2(centerX, gapBottom), ImVec2(centerX, rulerY + 200.f), g_ColorWhite, 2.0f);

    // --- Heading label centred in the gap ---
    ImGui::PushFont(g_FontArial20);
    draw_list->AddText(ImVec2(capTextX, capTextY), g_ColorWhite, headingText.c_str());
    ImGui::PopFont();
}
void RenderNameOfTextures()
{
    if (!g_bShowTextures)
        return;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    int side = g_Ocean->FFT_SIZE + 5;

    const char* names[] = { "Displacements", "Gradients", "Foam Buffer" };

    for (int i = 0; i < 3; i++)
    {
        float x = 5.0f + i * side;
        float y = (float)(g_WindowH - side - 10);

        ImVec2 text_pos = GetLeftAlignedTextPos(names[i], ImVec2(x, y), 4.0f, 0.0f, g_FontArial16);

        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 220), names[i]);
    }
}
void RenderSpectrum()
{
    if (!g_bShowOceanAnalysisWindow)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize = io.DisplaySize;

    float marginL = 70.0f, marginR = 40.0f, marginT = 50.0f, marginB = 60.0f;
    float graphW = windowSize.x * 0.45f;
    float graphH = windowSize.y * 0.35f;
    ImVec2 origin(windowSize.x * 0.5f - graphW * 0.5f, windowSize.y * 0.5f - graphH * 0.5f - 180.f);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(windowSize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##Spectrum", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec2 pMin(origin.x + marginL, origin.y + marginT);
    ImVec2 pMax(origin.x + graphW - marginR, origin.y + graphH - marginB);
    float gW = pMax.x - pMin.x;
    float gH = pMax.y - pMin.y;

    // --- Background panel ---
    ImVec2 panelMin(origin.x + 5, origin.y + 5);
    ImVec2 panelMax(origin.x + graphW - 5, origin.y + graphH - 5);
    draw->AddRectFilledMultiColor(panelMin, panelMax, IM_COL32(5, 5, 20, 120), IM_COL32(5, 5, 20, 120), IM_COL32(15, 15, 40, 140), IM_COL32(15, 15, 40, 140));
    draw->AddRect(panelMin, panelMax, IM_COL32(60, 80, 120, 160), 0, 0, 2.0f);

    // --- Graph background ---
    draw->AddRectFilled(pMin, pMax, IM_COL32(8, 8, 25, 200), 3.0f);
    draw->AddRect(pMin, pMax, IM_COL32(80, 100, 150, 220), 3.0f, 0, 1.5f);

    // =========================================================
    // SPECTRUM S(k) — logarithmic X axis [kMin, kMax=2]
    // =========================================================
    const float g_grav = 9.81f;
    const int   Nk = 300;
    const float kMin = 0.001f;
    const float kMax = 2.0f;   // ← limited to 2

    vec2 windDir = glm::length(g_Ocean->Wind) > 1e-6f ? glm::normalize(g_Ocean->Wind) : vec2(1.0f, 0.0f);

    vector<float> kvals(Nk), svals(Nk);
    for (int i = 0; i < Nk; ++i)
    {
        float t = (float)i / (Nk - 1);
        float k = kMin * powf(kMax / kMin, t);
        kvals[i] = k;
        vec2 kv = windDir * k;
        svals[i] = (g_Ocean.get()->*(g_Ocean->CurrentSpectrum))(kv);
    }

    float sRefK = *std::max_element(svals.begin(), svals.end());
    if (sRefK < 1e-30f) sRefK = 1.0f;
    for (auto& s : svals) s /= sRefK;

    float sMax = 1.1f;

    auto toScreenK = [&](float k, float s) -> ImVec2 {
        float xn = (logf(k) - logf(kMin)) / (logf(kMax) - logf(kMin));
        float x = pMin.x + xn * gW;
        float y = pMax.y - (s / sMax) * gH;
        return ImVec2(x, y);
        };

    // --- Horizontal grid ---
    const int nGridH = 5;
    for (int gi = 0; gi <= nGridH; ++gi)
    {
        float y = pMin.y + gH * gi / nGridH;
        float val = sMax * (1.0f - (float)gi / nGridH);
        draw->AddLine(ImVec2(pMin.x, y), ImVec2(pMax.x, y), IM_COL32(50, 50, 90, 120), 1.0f);
        char buf[32]; snprintf(buf, sizeof(buf), "%.2f", val);
        ImVec2 tSz = ImGui::CalcTextSize(buf);
        draw->AddText(ImVec2(pMin.x - tSz.x - 10, y - tSz.y * 0.5f), IM_COL32(200, 200, 240, 255), buf);
    }

    // --- Vertical grid (logarithmic) ---
    const float kTicks[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f };
    for (float kt : kTicks)
    {
        if (kt < kMin || kt > kMax) continue;
        float xn = (logf(kt) - logf(kMin)) / (logf(kMax) - logf(kMin));
        float x = pMin.x + xn * gW;
        draw->AddLine(ImVec2(x, pMin.y), ImVec2(x, pMax.y), IM_COL32(50, 50, 90, 120), 1.0f);
        char buf[16];
        if (kt < 0.1f)      snprintf(buf, sizeof(buf), "%.2f", kt);
        else if (kt < 1.0f) snprintf(buf, sizeof(buf), "%.1f", kt);
        else                snprintf(buf, sizeof(buf), "%.0f", kt);
        ImVec2 tSz = ImGui::CalcTextSize(buf);
        draw->AddText(ImVec2(x - tSz.x * 0.5f, pMax.y + 6), IM_COL32(200, 200, 240, 255), buf);
    }

    // --- Area S(k) ---
    {
        vector<ImVec2> fillPoly;
        fillPoly.push_back(ImVec2(pMin.x, pMax.y));
        for (int i = 0; i < Nk; ++i)
            fillPoly.push_back(toScreenK(kvals[i], svals[i]));
        fillPoly.push_back(ImVec2(pMax.x, pMax.y));
        if (fillPoly.size() > 2)
            draw->AddConcavePolyFilled(fillPoly.data(), (int)fillPoly.size(), IM_COL32(40, 90, 200, 100));
    }

    // --- Curve S(k) ---
    {
        vector<ImVec2> curve;
        for (int i = 0; i < Nk; ++i)
            curve.push_back(toScreenK(kvals[i], svals[i]));
        if (curve.size() > 1)
            draw->AddPolyline(curve.data(), (int)curve.size(), IM_COL32(100, 200, 255, 255), ImDrawFlags_None, 2.5f);
    }

    // --- Peak S(k) ---
    {
        auto it_max = std::max_element(svals.begin(), svals.end());
        int  iPic = (int)std::distance(svals.begin(), it_max);
        float kPeak = kvals[iPic];
        float sPeak = svals[iPic];
        ImVec2 picPt = toScreenK(kPeak, sPeak);

        for (float y = picPt.y; y < pMax.y; y += 8.0f)
            draw->AddLine(ImVec2(picPt.x, y), ImVec2(picPt.x, y + 4.0f), IM_COL32(255, 200, 50, 200), 1.5f);

        draw->AddCircleFilled(picPt, 6.0f, IM_COL32(255, 230, 60, 255));
        draw->AddCircle(picPt, 6.0f, IM_COL32(255, 255, 255, 240), 0, 2.0f);

        float lambdaPeak = 2.0f * (float)M_PI / kPeak;
        char buf[64];
        snprintf(buf, sizeof(buf), "Lp = %.1f m  (kp = %.3f)", lambdaPeak, kPeak);
        ImVec2 tSz = ImGui::CalcTextSize(buf);
        float lx = picPt.x + 12;
        if (lx + tSz.x > pMax.x) lx = picPt.x - tSz.x - 12;
        ImVec2 labelPos(lx, picPt.y - tSz.y * 0.5f - 2);
        draw->AddRectFilled(ImVec2(labelPos.x - 4, labelPos.y - 2), ImVec2(labelPos.x + tSz.x + 6, labelPos.y + tSz.y + 4), IM_COL32(255, 220, 50, 200), 4.0f);
        draw->AddRect(ImVec2(labelPos.x - 4, labelPos.y - 2), ImVec2(labelPos.x + tSz.x + 6, labelPos.y + tSz.y + 4), IM_COL32(255, 255, 255, 220), 4.0f, 0, 1.2f);
        draw->AddText(labelPos, IM_COL32(20, 20, 40, 255), buf);
    }

    // =========================================================
    // SPECTRUM S(f) — superimposed, same X log scale via k=f²/g
    // Independent normalization, plotted in red dashed line
    // =========================================================
    if (!g_Ocean->a_Frequences.empty() && !g_Ocean->a_SpectreAccumule.empty())
    {
        const auto& freq = g_Ocean->a_Frequences;
        const auto& spec = g_Ocean->a_SpectreAccumule;
        size_t N = spec.size();

        // Peak S(f) (skip DC)
        auto it_maxF = std::max_element(spec.begin() + 1, spec.end());
        double sRefF = *it_maxF;
        if (sRefF < 1e-30) sRefF = 1.0;

        // Conversion f → k via deep water dispersion relation: k = (2πf)² / g
        auto freqToK = [&](double f) -> float {
            double omega = 2.0 * M_PI * f;
            return (float)(omega * omega / g_grav);
            };

        // Curve S(f) in red dashed line on the k axis  
        // We iterate over the frequencies, convert each f to k, filter on [kMin, kMax], normalize S(f) to [0,1]
        vector<ImVec2> curveF;
        for (size_t i = 1; i < N; ++i)
        {
            float k = freqToK(freq[i]);
            if (k < kMin || k > kMax) continue;
            float sNorm = (float)(spec[i] / sRefF); // normalized [0,1]
            curveF.push_back(toScreenK(k, sNorm));
        }

        // Draw in red dashed line
        if (curveF.size() > 1)
        {
            const float dashLen = 6.0f;
            const float gapLen = 4.0f;
            float accumLen = 0.0f;
            bool  drawing = true;

            for (size_t i = 1; i < curveF.size(); ++i)
            {
                ImVec2 p0 = curveF[i - 1];
                ImVec2 p1 = curveF[i];
                float dx = p1.x - p0.x;
                float dy = p1.y - p0.y;
                float segLen = sqrtf(dx * dx + dy * dy);
                if (segLen < 0.001f) continue;
                float nx = dx / segLen;
                float ny = dy / segLen;

                float walked = 0.0f;
                while (walked < segLen)
                {
                    float remain = drawing ? (dashLen - accumLen) : (gapLen - accumLen);
                    float step = fminf(remain, segLen - walked);

                    if (drawing)
                    {
                        ImVec2 a(p0.x + nx * walked, p0.y + ny * walked);
                        ImVec2 b(p0.x + nx * (walked + step), p0.y + ny * (walked + step));
                        draw->AddLine(a, b, IM_COL32(255, 80, 80, 240), 2.0f);
                    }

                    walked += step;
                    accumLen += step;

                    if (drawing && accumLen >= dashLen - 0.001f)
                    {
                        accumLen = 0.0f;
                        drawing = false;
                    }
                    else if (!drawing && accumLen >= gapLen - 0.001f)
                    {
                        accumLen = 0.0f;
                        drawing = true;
                    }
                }
            }
        }

        // Peak S(f) — red point
        {
            size_t iPicF = std::distance(spec.begin(), it_maxF);
            float  kPicF = freqToK(freq[iPicF]);
            if (kPicF >= kMin && kPicF <= kMax)
            {
                ImVec2 picPt = toScreenK(kPicF, 1.0f); // normalisé = 1 au pic
                double Tp = 1.0 / freq[iPicF];

                for (float y = picPt.y; y < pMax.y; y += 8.0f)
                    draw->AddLine(ImVec2(picPt.x, y), ImVec2(picPt.x, y + 4.0f), IM_COL32(255, 80, 80, 200), 1.5f);

                draw->AddCircleFilled(picPt, 6.0f, IM_COL32(255, 60, 60, 255));
                draw->AddCircle(picPt, 6.0f, IM_COL32(255, 255, 255, 240), 0, 2.0f);

                char buf[64];
                snprintf(buf, sizeof(buf), "Tp = %.2f s", Tp);
                ImVec2 tSz = ImGui::CalcTextSize(buf);
                float lx = picPt.x + 12;
                if (lx + tSz.x > pMax.x) lx = picPt.x - tSz.x - 12;
                char bufK[64];
                snprintf(bufK, sizeof(bufK), "Lp = %.1f m  (kp = %.3f)", 2.0f * (float)M_PI / kPicF, kPicF);
                ImVec2 tSzK = ImGui::CalcTextSize(bufK);
                float yellowBoxH = tSzK.y + 6.0f; // hauteur boite jaune (padding haut+bas = 2+4)

                ImVec2 labelPos(lx, picPt.y - tSz.y * 0.5f - 2 + yellowBoxH);
                draw->AddRectFilled(ImVec2(labelPos.x - 4, labelPos.y - 2), ImVec2(labelPos.x + tSz.x + 6, labelPos.y + tSz.y + 4),IM_COL32(220, 60, 60, 200), 4.0f);
                draw->AddRect(ImVec2(labelPos.x - 4, labelPos.y - 2), ImVec2(labelPos.x + tSz.x + 6, labelPos.y + tSz.y + 4), IM_COL32(255, 255, 255, 220), 4.0f, 0, 1.2f);
                draw->AddText(labelPos, IM_COL32(255, 255, 255, 255), buf);
            }
        }
    }

    // =========================================================
    // LEGEND
    // =========================================================
    {
        float lx = pMin.x + 10;
        float ly = pMin.y + 10;

        // S(k) — bleu
        draw->AddLine(ImVec2(lx, ly + 7), ImVec2(lx + 24, ly + 7), IM_COL32(100, 200, 255, 255), 2.5f);
        draw->AddText(ImVec2(lx + 30, ly), IM_COL32(200, 200, 240, 255), "S(k) theoretical");

        ly += 20;

        // S(f) — red dashed line
        for (float dx = 0; dx < 24; dx += 10.0f)
            draw->AddLine(ImVec2(lx + dx, ly + 7), ImVec2(lx + dx + 6, ly + 7), IM_COL32(255, 80, 80, 240), 2.0f);
        draw->AddText(ImVec2(lx + 30, ly), IM_COL32(200, 200, 240, 255), "S(f) measured");
    }

    // --- Title ---
    const char* title = "Wave Spectrum S(k) & S(f)";
    ImVec2 tSzTitle = ImGui::CalcTextSize(title);
    ImVec2 titlePos(pMin.x + gW * 0.5f - tSzTitle.x * 0.5f, origin.y + 10);
    draw->AddRectFilled(ImVec2(titlePos.x - 6, titlePos.y - 4), ImVec2(titlePos.x + tSzTitle.x + 6, titlePos.y + tSzTitle.y + 2), IM_COL32(20, 30, 50, 160), 3.0f);
    draw->AddText(titlePos, IM_COL32(240, 240, 255, 255), title);

    // --- Label X axis ---
    const char* xLabel = "Wavenumber k (rad/m)";
    ImVec2 xSz = ImGui::CalcTextSize(xLabel);
    draw->AddText(ImVec2(pMin.x + gW * 0.5f - xSz.x * 0.5f, pMax.y + 30), IM_COL32(200, 200, 240, 255), xLabel);

    // --- Label Y axis ---
    const char* yLabel = "S(k) / S(f)  normalized";
    ImVec2 ySz = ImGui::CalcTextSize(yLabel);
    draw->AddText(ImVec2(pMin.x - ySz.x * 0.5f, pMin.y - ySz.y - 6), IM_COL32(180, 180, 220, 220), yLabel);

    ImGui::End();
}
void FrameImGui(VkCommandBuffer commandBuffer)
{
    auto RightAlignText = [&](const char* fmt, ...) {
        char buf[64];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        float text_w = ImGui::CalcTextSize(buf).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - text_w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::Text("%s", buf);
        };

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    static string resultAnalyse;
    static vector<vector<sResultData>> vResults;

#pragma region Scene F2
    if (g_bShowSceneWindow)
    {
        // Fixed position at the bottom right of the window
        ImVec2 window_size(300, 930);
        ImVec2 window_pos(5, 5);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Scene [F2]", &g_bShowSceneWindow))
        {
            ImGui::Checkbox("VSYNC", &g_bTargetFps);
            ImGui::SameLine();
            ImGui::Text("-> FPS = %d", g_nFps);
            ImGui::SameLine();
            double elapsed = glfwGetTime();  // seconds elapsed since the start of the program or ImGui
            int hours = static_cast<int>(elapsed) / 3600;
            int minutes = (static_cast<int>(elapsed) % 3600) / 60;
            int seconds = static_cast<int>(elapsed) % 60;
            char buffer[12];
            snprintf(buffer, sizeof(buffer), "  %02d:%02d:%02d", hours, minutes, seconds);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 128, 255));
            ImGui::SetWindowFontScale(1.5f);
            ImGui::Text("%s", buffer);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            // ------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
            // Camera type
            switch (g_Camera.GetMode())
            {
            case eCameraMode::ORBITAL:  ImGui::Text("[ ORBITAL ]"); break;
            case eCameraMode::BRIDGE:   ImGui::Text("[ BRIDGE ]"); break;
            case eCameraMode::FPS:      ImGui::Text("[ FPS ]");     break;
            }
            ImGui::SameLine();
            // Interpolation function
            switch (g_Camera.GetInterpolation())
            {
            case eInterpolation::Linear:    ImGui::Text("( Linear )");      break;
            case eInterpolation::SmoothStep:ImGui::Text("( SmoothStep )");  break;
            case eInterpolation::EaseIn:    ImGui::Text("( EaseIn )");      break;
            case eInterpolation::EaseOut:   ImGui::Text("( EaseOut )");     break;
            case eInterpolation::EaseInOut: ImGui::Text("( EaseInOut )");   break;
            }
            ImGui::PopStyleColor(1);
            // ------------
            ImGui::Checkbox("Night vision", &g_bNightVision);
            ImGui::SameLine();
            ImGui::Checkbox("Low vision", &g_bLowIntensity);
            // ------------
            if (ImGui::Button(" PAUSE "))
            {
                g_bPause = !g_bPause;
                if (g_bPause)
                    g_TimerScene.stop();
                else
                    g_TimerScene.start();
            }
            ImGui::SameLine();
            if (ImGui::Button(" FULLSCREEN "))
                g_PendingFullscreen = true;
            
            /////////////////////////////////
            if (ImGui::CollapsingHeader("SCENE", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (g_Ocean) ImGui::Checkbox("Ocean", &g_Ocean->bVisible);
                ImGui::SameLine();
                if (g_Sky) ImGui::Checkbox("Sky", &g_Sky->bVisible);
                ImGui::SameLine();
                if (g_Clouds) ImGui::Checkbox("Clouds", &g_Clouds->bVisible);
                ImGui::SameLine();
                if (ImGui::Checkbox("Ship", &g_Ship->bVisible))
                    g_bShip = g_Ship->bVisible;
                // ------------
                ImGui::Checkbox("Sound##1", &g_SoundMgr->bSound);
                ImGui::SameLine();
                ImGui::Checkbox("Seagull", &g_bSoundSeagull);
                ImGui::SameLine();
                ImGui::Checkbox("Wind Arrow", &g_ArrowWind->bVisible);
                // ------------
                if (g_vTerrains.size()) {
                    ImGui::Checkbox("Terrain", &g_bShowTerrain);
                    ImGui::SameLine();
                }
                ImGui::Checkbox("Markup", &g_Markup->bVisible);
                if (g_Axis) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Axis", &g_Axis->bVisible);
                }
                // ------------
                if (ImGui::Checkbox("Manoeuvrability", &g_bShowManoeuver))
					g_Grid->bVisible = g_bShowManoeuver;
                ImGui::SameLine();
                ImGui::Checkbox("Traffic", &g_Traffics->bVisible);
            }

            /////////////////////////////////
            if (ImGui::CollapsingHeader("WIND"))
            {
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));         // Red
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));   // Dark red when active
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));            // Dark background
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));     // Lighter background on hover
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));      // Even brighter background when active
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));               // White text
                if (ImGui::SliderFloat("Direction", &g_TWS_Deg, 0.0f, 360.0f, "%.f\xc2\xb0"))
                {
                    g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
                    g_Ocean->SetWind(g_Wind);
                    g_Ocean->InitFrequencies();
                }
                if (ImGui::SliderFloat("Speed##1", &g_TWS_Kt, 1.0f, 30.0f, "%0.0f kt"))
                {
                    g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
                    g_Ocean->SetWind(g_Wind);
                    g_Ocean->InitFrequencies();
                    g_Clouds->SetCloudSpeed(g_TWS_Kt);
                }
                ImGui::PopStyleColor(6);                                                            // For the 6 PushStyleColor above
            }

            /////////////////////////////////
            if (ImGui::CollapsingHeader("OCEAN", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (g_Ocean)
                {
                    ImGui::SliderFloat("Lambda", &g_Ocean->Lambda, 0.0f, 1.0f, "%0.2f");
                    if (ImGui::SliderFloat("Foam", &g_Ocean->PersistenceSec, 0.0f, 5.0f, "%0.1f s")) g_Ocean->EvaluatePersistence(g_Ocean->PersistenceSec);
                    ImGui::Text("Whitecap theor. %.3f %% - real %.3f %%", g_Ocean->WhitecapCoverageTheoretical, g_Ocean->WhitecapCoverageReal);
                    ImGui::SliderFloat("Transparency", &g_Ocean->Transparency, 0.0f, 1.0f, "%0.2f");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                    if (ImGui::BeginCombo("Spectre##1", g_Ocean->SpectrumNames[g_Ocean->Spectre]))
                    {
                        for (int i = 0; i < IM_ARRAYSIZE(g_Ocean->SpectrumNames); i++)
                        {
                            bool selected = (g_Ocean->Spectre == i);
                            if (ImGui::Selectable(g_Ocean->SpectrumNames[i], selected))
                            {
                                g_Ocean->SetSpectrum(i);
                                g_Ocean->NeedsReinitFrequencies.store(true);
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopStyleColor();

                    if (ImGui::SliderFloat("Amplitude", &g_Ocean->Amplitude, 0.0f, 3.0f, "%0.2f"))
                        g_Ocean->NeedsClearRecords.store(true);
     
                    // Philipps, Bretschneider, Pierson-Moskowitz, JONSWAP, OchiHubble
                    bool hasDirSpread = (g_Ocean->Spectre < 5);    
                    ImGui::BeginDisabled(!hasDirSpread);
                    if (ImGui::SliderFloat("Dir Spread", &g_Ocean->DirSpread, 0.0f, 6.0f, "%0.1f"))
                        g_Ocean->NeedsReinitFrequencies.store(true);
                    ImGui::EndDisabled();

                    // JONSWAP,	Texel-Marsen-Arsloe, Donelan-Banner, Elfouhaily, Horvath
                    bool hasMaturity = (g_Ocean->Spectre == 3 || g_Ocean->Spectre == 5 || g_Ocean->Spectre == 6 || g_Ocean->Spectre == 8 || g_Ocean->Spectre == 9);
                    ImGui::BeginDisabled(!hasMaturity); 
                    if (ImGui::SliderFloat("Maturity", &g_Ocean->Maturity, 1.0f, 10.0f, "%0.1f"))
                        g_Ocean->NeedsReinitFrequencies.store(true);
                    ImGui::EndDisabled();

                    // Not Philipps and not Pierson-Moskowitz
                    bool hasFetch = (g_Ocean->Spectre != 0 && g_Ocean->Spectre != 2);   
                    ImGui::BeginDisabled(!hasFetch);
                    int fetchKm = (int)(g_Ocean->Fetch / 1000.0f);
                    if (ImGui::SliderInt("Fetch", &fetchKm, 1, 1000, "%d km")) {
                        g_Ocean->Fetch = (float)(fetchKm * 1000);
                        g_Ocean->NeedsReinitFrequencies.store(true);
                    }
                    ImGui::EndDisabled();
                    
                    // Texel-Marsen-Arsloe
                    bool hasDepth = (g_Ocean->Spectre == 5);
                    ImGui::BeginDisabled(!hasDepth); 
                    if (ImGui::SliderFloat("Depth", &g_Ocean->Depth, 1.0f, 100.0f, "%0.0f m"))
                        g_Ocean->NeedsReinitFrequencies.store(true);
                    ImGui::EndDisabled();

                    // Colors
                    if (ImGui::SliderInt("Color##1", &g_Ocean->iOceanColor, 0, g_Ocean->vOceanColors.size() - 1))
                        g_Ocean->OceanColor = g_Ocean->vOceanColors[g_Ocean->iOceanColor];
                    ImGui::ColorEdit3("Ocean", (float*)&g_Ocean->OceanColor[0], 0);
                    ImGui::Checkbox("Envmap", &g_Ocean->bEnvMap);
                    ImGui::SameLine();
                    ImGui::Checkbox("Patches", &g_Ocean->bShowPatches);
                    ImGui::SameLine();
                    ImGui::Checkbox("Wireframe", &g_Ocean->bWireframe);

                    ImGui::Checkbox("Spectre", &g_bShowOceanAnalysisWindow);
                    // Heights
                    static float waves1_3 = 0.0f;
                    static float waveMax = 0.0f;
                    static float average_period = 0.0f;
                    static int nWaves = 0;
                    g_Ocean->GetWaveByWaveAnalysis(waves1_3, waveMax, nWaves, average_period);
                    ImGui::Text("Height 1/3  : %.1f m (%.1f s) (%d waves)", waves1_3, average_period, nWaves);

                    ImGui::Text("Height max  : %.1f m | min : %.1f m", g_Ocean->HeightMax, g_Ocean->HeightMin);
                    static double lastUpdateTime = 0.0;
                    if (g_bShowOceanAnalysisWindow)
                    {
                        double currentTime = glfwGetTime(); // current time in seconds since GLFW initialization
                        if (currentTime - lastUpdateTime >= 1.0)
                        {
                            lastUpdateTime = currentTime;
                            vResults.clear();
                            vResults.push_back(g_Ocean->SpectralAnalysis());
                            vResults.push_back(g_Ocean->DirectionalAnalysis());
                        }
                    }
                    else
                        lastUpdateTime = 0.0;

                    auto waveParams = JONSWAPModel::GetWaveParameters(knot_to_ms(g_TWS_Kt), g_Ocean->Fetch);
                    ImGui::Text("JONSWAP 1/3 : %.1f m (%.1f s)", waveParams.significantWaveHeight, waveParams.peakPeriod);
                }
            }

            /////////////////////////////////
            if (ImGui::CollapsingHeader("SUN"))
            {
                ImGui::ColorEdit3("Ambient", (float*)&g_Sky->SunAmbient[0], 0);
                ImGui::ColorEdit3("Diffuse", (float*)&g_Sky->SunDiffuse[0], 0);
                if (g_Sky) ImGui::SliderFloat("Exposure", &g_Sky->Exposure, 0.0f, 3.0f, "%0.2f");

                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));         // Red
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));   // Dark red when active
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));            // Dark background
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));     // Lighter background on hover
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));      // Even brighter background when active
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));               // White text

                static sHM hm = g_Sky->GetNow();
                sHM storeHM = hm;
                float hour = hm.hour + hm.minute / 60.0f;
                if (ImGui::SliderFloat("Time", &hour, 0.0f, 24.0f, "", ImGuiSliderFlags_None))
                {
                    hm.hour = int(hour);
                    hm.minute = fract(hour) * 60.0f + 0.5f;
                    if (hm.hour != storeHM.hour || hm.minute != storeHM.minute)
                        g_Sky->SetTime(hm.hour, hm.minute);
                }
                char buf[16];
                snprintf(buf, 16, "%02d:%02d", hm.hour, hm.minute);
                ImGui::SameLine();
                ImGui::Text("%s", buf);

                ImGui::PopStyleColor(6);                                                        // For the 6 PushStyleColor above

                if (ImGui::Button(" NOW "))
                {
                    g_Sky->SetNow();
                    sHM hm1 = g_Sky->GetNow();
                    hour = hm1.hour + hm1.minute / 60.0f;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Sky 2008", RENDER_SKY == 1)) { RENDER_SKY = 1; g_Clouds->PostDescriptorsInitialized = false; }
                ImGui::SameLine();
                if (ImGui::RadioButton("Sky 2017", RENDER_SKY == 2)) { RENDER_SKY = 2; g_Clouds->PostDescriptorsInitialized = false; }
                if (RENDER_SKY == 2)
                {
                    float rayScaleHeight = -1.0 / g_Sky->GetRayleighDensity();
                    ImGui::SliderFloat("Rayleigh", &rayScaleHeight, 0.1, 20.0);
                    g_Sky->SetRayleighDensity(-1.0 / rayScaleHeight);

                    float mieScaleHeight = -1.0 / g_Sky->GetMieDensity();
                    ImGui::SliderFloat("Mie", &mieScaleHeight, 0.1, 20.0);
                    g_Sky->SetMieDensity(-1.0 / mieScaleHeight);
                }
            }
            /////////////////////////////////
            if (ImGui::CollapsingHeader("ATMOSPHERE", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Separator();

                if (ImGui::SliderFloat("Mist", &g_Sky->MistDensity, 0.0f, 0.001f, "%.5f"))
                {
                    if (g_Sky->MistDensity > 0.0f)
                        g_Sky->FogDensity = 0.0f;   // Disables Fog if Mist > 0
                }
                auto toLog = [](float v) { return v > 0.0f ? log10(v) : -5.0f; };
                auto fromLog = [](float v) { return pow(10.0f, v); };
                float fogLog = toLog(g_Sky->FogDensity);
                char fogLabel[32];
                snprintf(fogLabel, sizeof(fogLabel), "%.6f", g_Sky->FogDensity);
                if (ImGui::SliderFloat("Fog", &fogLog, -5.0f, -2.0f, fogLabel))
                {
                    g_Sky->FogDensity = fromLog(fogLog);
                    if (g_Sky->FogDensity > 0.0f)
                        g_Sky->MistDensity = 0.0f;
                }
                if (ImGui::Checkbox("Rain", &g_Sky->bRain))
                {
                    if (g_Sky->bRain)
                    {
                        g_Sky->StoreMistDensity = g_Sky->MistDensity; // Store the current mist density as well
                        g_Sky->StoreFogDensity = g_Sky->FogDensity;  // Store the current fog density before changing it for rain
                        g_Sky->MistDensity = 0.0f; // Disable mist when rain is enabled
                        g_Sky->FogDensity = 0.001f;
                        if (g_SoundMgr->bSound)
                            g_SoundRain->play();
                    }
                    else
                    {
						g_Sky->FogDensity = g_Sky->StoreFogDensity; // recall the previous value when rain is turned off
                        g_Sky->MistDensity = g_Sky->StoreMistDensity;
                        g_SoundRain->stop();
                    }
                }
            }

            /////////////////////////////////
            if (ImGui::CollapsingHeader("CLOUDS", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat("Coverage", &g_Clouds->Coverage, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Speed##3", &g_Clouds->CloudSpeed, 0.0f, 2000.0f, "%.0f");
                ImGui::SliderFloat("Crispiness", &g_Clouds->Crispiness, 0.0f, 120.0f, "%.0f");
                ImGui::SliderFloat("Curliness", &g_Clouds->Curliness, 0.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("Illumination", &g_Clouds->Illumination, 0.5f, 5.0f, "%.1f");
                ImGui::SliderFloat("Absorption", &g_Clouds->Absorption, 0.0f, 1.5f, "%.2f");
                ImGui::SliderFloat("Top", &g_Clouds->SphereOuterRadius, 1000.0f, 40000.0f, "%.0f");
                ImGui::SliderFloat("Bottom", &g_Clouds->SphereInnerRadius, 1000.0f, 15000.0f, "%.0f");
                if (ImGui::SliderFloat("Frequency", &g_Clouds->PerlinFrequency, 0.0f, 4.0f, "%.2f"))
                    g_Clouds->ComputeNewWeatherLUT();
                ImGui::ColorEdit3("Cloud top", (float*)&g_Clouds->CloudColorTop[0], 0);
                ImGui::ColorEdit3("Cloud bottom", (float*)&g_Clouds->CloudColorBottom[0], 0);
            }
        }
        ImGui::End();
    }
#pragma endregion

#pragma region Ship F3
    if (g_bShowShipWindow && g_Ship.get())
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 250.0f - 5.0f, 5.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(250, 275), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Ship [F3]", &g_bShowShipWindow))
        {
            string temp;
            if (g_vShips.size() && g_NoShip >= 0 && g_NoShip < g_vShips.size())
                temp = g_vShips[g_NoShip].ShortName;
            const char* combo_preview_value = temp.c_str();
            if (ImGui::BeginCombo(" ", combo_preview_value, 0))
            {
                for (int n = 0; n < g_vShips.size(); n++)
                {
                    const bool is_selected = (g_NoShip == n);
                    if (ImGui::Selectable(g_vShips[n].ShortName.c_str(), is_selected))
                        g_PendingNoShipChange = n;

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            // ------------
            ImGui::Checkbox("Model", &g_Ship->bModel);
            ImGui::SameLine();
            ImGui::Checkbox("Sound##2", &g_Ship->bSound);
            ImGui::SameLine();
            ImGui::Checkbox("Lights", &g_Ship->bLights);
            // ------------
            ImGui::Checkbox("Kelvin", &g_bShipKelvinWake);
            ImGui::SameLine();
            ImGui::Checkbox("Wake", &g_bShipWake);
            // ------------
            ImGui::Checkbox("Shadow", &g_bShipShadow);
            ImGui::SameLine();
            ImGui::Checkbox("Reflection", &g_bShipReflection);
            // ------------
            ImGui::Checkbox("Spray", &g_Ship->bSpray);
            ImGui::SameLine();
            ImGui::Checkbox("Smoke", &g_Ship->bSmoke);
            ImGui::SameLine();
            ImGui::Checkbox("Flag", &g_Ship->bFlag);
            /////////////////////////////////
            ImGui::SeparatorText("DEBUG");

            ImGui::Checkbox("HullMesh", &g_Ship->bHullMesh);
            ImGui::SameLine();
            ImGui::Checkbox("Axis", &g_Ship->bAxis);
            ImGui::SameLine();
            ImGui::Checkbox("BBox", &g_Ship->bBbox);
            // ------------
            ImGui::Checkbox("Wake##2", &g_Ship->bWakeMesh);
            ImGui::SameLine();
            ImGui::Checkbox("Contours", &g_Ship->bContour);
            // ------------
            ImGui::Checkbox("Pressure", &g_Ship->bPressure);
            ImGui::SameLine();
            ImGui::Checkbox("Traffic", &g_Traffics->bShowRoute);
            // ------------
            ImGui::Checkbox("Wireframe##2", &g_Ship->bWireframe);

            /////////////////////////////////
            ImGui::SeparatorText("AUTOPILOT");
            if (ImGui::Button("SETTINGS"))
                g_bShowAutopilotWindow = !g_bShowAutopilotWindow;

            /////////////////////////////////
            ImGui::SeparatorText("PHYSICS");

            const char* preview_name = g_vPositions[g_NoPosition].name.c_str();
            bool changed = false;
            if (ImGui::BeginCombo("##combo", preview_name))
            {
                for (int n = 0; n < (int)g_vPositions.size(); n++)
                {
                    const bool is_selected = (g_NoPosition == n);
                    // Display text with the name + position
                    std::string item_text = g_vPositions[n].name;
                    if (ImGui::Selectable(item_text.c_str(), is_selected))
                    {
                        g_NoPosition = n;
                        changed = true;
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (changed)
                SetPosition();

            if (ImGui::Checkbox("Motion", &g_Ship->bMotion))
                g_Ship->ResetVelocities();
            // ------------
            if (ImGui::Button("S"))
                SetHeading(180.0f);
            ImGui::SameLine();
            if (ImGui::Button("SW"))
                SetHeading(225.0f);
            ImGui::SameLine();
            if (ImGui::Button("W"))
                SetHeading(270.0f);
            ImGui::SameLine();
            if (ImGui::Button("NW"))
                SetHeading(315.0f);
            ImGui::SameLine();
            if (ImGui::Button("N"))
                SetHeading(0.0f);
            ImGui::SameLine();
            if (ImGui::Button("NE"))
                SetHeading(45.0f);
            ImGui::SameLine();
            if (ImGui::Button("E"))
                SetHeading(90.0f);
            ImGui::SameLine();
            if (ImGui::Button("SE"))
                SetHeading(135.0f);

            ImGui::SliderFloat3("Gravity", (float*)&g_Ship->ship.PosGravity[0], -10.0f, 5.0f, "%.1f");
            int massT = g_Ship->ship.Mass_t;
            if (ImGui::SliderInt("Mass", &massT, g_LowMass, g_HighMass, "%d t"))
            {
                g_Ship->ship.Mass_t = massT;
                g_Ship->SetMass();
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.0f, 1.0f));
            ImGui::Text(g_Ship->InfoModel.c_str());
            ImGui::PopStyleColor();

            ImGui::SeparatorText("MODEL");

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
            ImGui::Text(g_Ship->Info3D.c_str());
            char txt[50];
            sprintf(txt, "Ocean Search Complexity : %d", g_Ship->WaterSearch);
            ImGui::Text(txt);
            ImGui::PopStyleColor();


        }

        ImGui::End();
    }
#pragma endregion

#pragma region Autopilot F5
    if (g_bShowAutopilotWindow)
    {
        ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2((g_WindowW - 350) / 2, g_WindowH - 300), ImGuiCond_Always);

        if (ImGui::Begin("Autopilot [F5]", &g_bShowAutopilotWindow))
        {
            ImGui::SliderFloat("P", &g_Ship->ship.BaseP, 0.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("I", &g_Ship->ship.BaseI, 0.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("D", &g_Ship->ship.BaseD, 0.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("MaxIntegral", &g_Ship->ship.MaxIntegral, 0.0f, 20.0f, "%.1f");
        }
        ImGui::End();
    }
#pragma endregion

#pragma region Ship forces F6
    if (g_bShowShipForcesWindows)
    {
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Ship Forces [F6]", &g_bShowShipForcesWindows))
        {
            ImGui::BeginTable("Forces", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
            // Table Headers
            ImGui::TableSetupColumn("VARIABLE", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("VALUE", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("UNIT", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableHeadersRow();

            // Table data
            for (const auto& result : g_Ship->vForcesData)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", result.variable.c_str());

                ImGui::TableSetColumnIndex(1);
                string format = "%." + to_string(result.decimal) + "f";
                if (abs(result.value) > 10000.0)
                {
                    format += " k";
                    ImGui::Text(format.c_str(), result.value / 1000.0);
                }
                else
                    ImGui::Text(format.c_str(), result.value);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", result.unit.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }
#pragma endregion

#pragma region Chronos F7
    if (g_bShowChronoWindow)
    {
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Chronos [F7]", &g_bShowChronoWindow))
        {
            // Table with 3 columns: Label | Value (int) | Units
            if (ImGui::BeginTable("PerfTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                // Headers
                ImGui::TableSetupColumn("METRIC GPU", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("TIME", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("UNIT", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < 5; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text(OceanTimeStamps[i].first.c_str());
                    ImGui::TableNextColumn(); RightAlignText("%.0f", (double)OceanTimeStamps[i].second / 1000.0);
                    ImGui::TableNextColumn(); ImGui::Text("\xc2\xb5s");
                }
                for (int i = 0; i < size(g_SceneTimeStamps); i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text(g_SceneTimeStamps[i].first.c_str());
                    ImGui::TableNextColumn(); RightAlignText("%.0f", (double)g_SceneTimeStamps[i].second / 1000.0);
                    ImGui::TableNextColumn(); ImGui::Text("\xc2\xb5s");
                }

                ImGui::EndTable();
            }
            // Table with 3 columns: Label | Value (int) | Units
            if (ImGui::BeginTable("PerfTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                // Headers
                ImGui::TableSetupColumn("METRIC CPU", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("TIME", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("UNIT", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < 2; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text(Chronos[i].name.c_str());
                    ImGui::TableNextColumn(); RightAlignText("%d", Chronos[i].GetMicroSecondes());
                    ImGui::TableNextColumn(); ImGui::Text("\xc2\xb5s");
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
#pragma endregion

#pragma region Ocean Analysis
    if (g_bShowOceanAnalysisWindow)
    {
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Spectrum Analysis", &g_bShowOceanAnalysisWindow))
        {
            ImGui::BeginTable("Results", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
            // Table Headers
            ImGui::TableSetupColumn("VARIABLE", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("VALUE", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("UNIT", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableHeadersRow();

            // Table data
            for (const auto& results : vResults)
            {
                for (const auto& result : results)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", result.variable.c_str());

                    ImGui::TableSetColumnIndex(1);
                    string format = "%." + to_string(result.decimal) + "f";
                    ImGui::Text(format.c_str(), result.value);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", result.unit.c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
#pragma endregion

    RenderDashboard();
    RenderShortcuts();  // F1
    RenderStatusbar();  // F4
    RenderInfo();
    RenderTitle();
    RenderNameOfTextures();
    if (g_bBinoculars)
        RenderCompassOfBinoculars();
    RenderSpectrum();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
}

// Render
void UpdateTerrains(uint32_t imageIndex)
{
    if (!g_bShowTerrain)
        return;

    for (auto& terrain : g_vTerrains)
    {
        mat4 model = glm::translate(mat4(1.0f), terrain.pos);
        terrain.model->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model, 0.5f, 0.0f);
    }
    if (g_Port.get())
    {
        mat4 model = glm::translate(mat4(1.0f), g_vTerrains[g_idxHouat].pos);    // The object is linked to Houat island
        g_Port->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model, 0.5f, 0.0f);
    }
    if (g_Pier.get())
    {
        mat4 model = glm::translate(mat4(1.0f), g_vTerrains[g_idxHoedic].pos);    // The object is linked to Hoedic island
        g_Pier->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model, 0.5f, 0.0f);
    }
}
void RenderTerrains(VkCommandBuffer commandBuffer)
{
    if (!g_bShowTerrain)
        return;

    for (auto& terrain : g_vTerrains)
        terrain.model->RenderMs(commandBuffer, g_iCurrentFrame);

    if (g_Port.get())
        g_Port->RenderMs(commandBuffer, g_iCurrentFrame);

	if (g_Pier.get())
        g_Pier->RenderMs(commandBuffer, g_iCurrentFrame);
}
void UpdateArrowWind(uint32_t imageIndex)
{
    vec3 position = vec3(g_Ship->ship.Position.x, 1.0f, g_Ship->ship.Position.z) + 2.0f * g_Ship->GetLength() * glm::normalize(vec3(g_Wind.x, 0.0f, g_Wind.y));
    float dir = g_TWS_Deg + 180.0f;
    dir = fmod(450.0f - dir, 360.0f);
    dir = glm::radians(dir);

    mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, vec3(1.0f));
    model = glm::rotate(model, dir, vec3(0, 1, 0));

    g_ArrowWind->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model, 0.5f, 0.0f);
}
void RenderTextures(VkCommandBuffer commandBuffer)
{
    // Textures of displacements, gradients & Foam buffer
    if (!g_bShowTextures)
        return;

    int side = g_Ocean->FFT_SIZE + 5;
    g_OverlayDisplacement->RenderOverlay(commandBuffer, g_Ocean->GetDisplacements(), vec2(5 + 0 * side, g_WindowH - side), vec2(512, 512));
    g_OverlayGradient->RenderOverlay(commandBuffer, g_Ocean->GetGradients(), vec2(5 + 1 * side, g_WindowH - side), vec2(512, 512));
    g_OverlayFoamBuffer->RenderOverlay(commandBuffer, g_Ocean->GetFoamBuffer(), vec2(5 + 2 * side, g_WindowH - side), vec2(512, 512));
}
void UpdateManoeuverZone(uint32_t imageIndex)
{
    if (!g_bShowManoeuver)
        return;
    
    float L = g_Ship->ship.Length;
    float scale = L / 20.0f;

	// IMO Turning Circle limits (advance 4.5L, tactical diameter 5L) for any ship > 100m long
    
    // Point 0°: (0, 0)
    mat4 model = glm::translate(mat4(1.0f), vec3(0.0f, 0.0f, 0.0f));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallRed[0]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);
    
    // Point 90°: (4.5L, 0)
    model = glm::translate(mat4(1.0f), vec3(4.5f * L, 0.0f, 0.0f));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallRed[1]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Point 180°: (4.5L, 5L)
    model = glm::translate(mat4(1.0f), vec3(4.5f * L, 0.0f, 5.0f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallRed[2]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);
    
    // Point 270°: (0, 5L)
    model = glm::translate(mat4(1.0f), vec3(0.0f, 0.0f, 5.0f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallRed[3]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Stopping ability
    
    model = glm::translate(mat4(1.0f), vec3(15.0f * L, 0.0f, 0.0f));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallRed[4]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Optimal circle (approximation 3L diameter, center at (2L, 1.5L))

    // Point 90° optimal: (3.5L, 0)
    model = glm::translate(mat4(1.0f), vec3(3.5f * L, 0.0f, 1.5f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallGreen[0]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Point 180° optimal: (2L, 3L)
    model = glm::translate(mat4(1.0f), vec3(2.0f * L, 0.0f, 3.0f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallGreen[1]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Point 270° optimal: (0.5L, 1.5L)
    model = glm::translate(mat4(1.0f), vec3(0.5f * L, 0.0f, 1.5f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallGreen[2]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

    // Point 360° optimal: (2L, 0)
    model = glm::translate(mat4(1.0f), vec3(2.0f * L, 0.0f, 0.0f * L));
    model = glm::scale(model, vec3(scale, scale, scale));
    g_BallGreen[3]->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), model);

	// Grid
    g_Grid->UpdateUBO(imageIndex, g_Camera, mat4(1.0f), vec4(1.0));
}
void RenderManoeuverZone(VkCommandBuffer commandBuffer)
{
    if (!g_bShowManoeuver)
        return;

    g_Grid->Render(commandBuffer, g_iCurrentFrame);
    
    for (uint32_t i = 0; i < 5; i++)
        g_BallRed[i]->RenderMsOpaque(commandBuffer, g_iCurrentFrame);

    for (uint32_t i = 0; i < 4; i++)
        g_BallGreen[i]->RenderMsOpaque(commandBuffer, g_iCurrentFrame);
}

void FrameRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    static bool bSaved = true;
    bool bAboveWater = g_Camera.GetPosition().y > 0.0f;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdResetQueryPool(commandBuffer, g_QueryPool, g_iCurrentFrame * NTT, NTT);

    // RENDER PASS 0 : SHADOW ///////////////////////////////////////////////////////////
    {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 0);

        VkViewport viewport{ 0, 0, (float)g_ShadowWidth, (float)g_ShadowHeight, 0, 1 };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ {0, 0}, {g_ShadowWidth, g_ShadowHeight} };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        g_Ship->UpdateShadowUBOs(imageIndex, g_Camera, g_Sky.get());

        VkClearValue clearDepth;
        clearDepth.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpInfo0{};
        rpInfo0.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo0.renderPass = g_RenderPassShadow;
        rpInfo0.framebuffer = g_FramebufferShadow;
        rpInfo0.renderArea.extent.width = g_ShadowWidth;
        rpInfo0.renderArea.extent.height = g_ShadowHeight;
        rpInfo0.renderArea.offset = { 0, 0 };
        rpInfo0.clearValueCount = 1;
        rpInfo0.pClearValues = &clearDepth;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo0, VK_SUBPASS_CONTENTS_INLINE);
        g_Ship->RenderShadow(commandBuffer, imageIndex);
        vkCmdEndRenderPass(commandBuffer);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 1);
    }

    // RENDER PASS 1 : WAKE ///////////////////////////////////////////////////////////
    {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 2);

        VkViewport viewport{ 0, 0, (float)g_WakeSize, (float)g_WakeSize, 0, 1 };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ {0, 0}, {g_WakeSize, g_WakeSize} };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        g_Ship->UpdateWakeUBO(imageIndex, g_WakeSize, g_WakeSize);

        VkClearValue clearColor{};
        clearColor.color = { 0.0f, 0.0f, 0.0f, 1.0f };

        VkRenderPassBeginInfo rpInfo1{};
        rpInfo1.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo1.renderPass = g_RenderPassWake;
        rpInfo1.framebuffer = g_FramebufferWake;
        rpInfo1.renderArea.offset = { 0, 0 };
        rpInfo1.renderArea.extent = VkExtent2D{ static_cast<unsigned int>(g_WakeSize), static_cast<unsigned int>(g_WakeSize) };
        rpInfo1.clearValueCount = 1;
        rpInfo1.pClearValues = &clearColor;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo1, VK_SUBPASS_CONTENTS_INLINE);
        g_Ship->RenderWake(commandBuffer, g_iCurrentFrame, imageIndex);
        vkCmdEndRenderPass(commandBuffer);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 3);
    }

    // CHANGE OF VIEWPORT ///////////////////////////////////////////////////////////

    float width = (float)g_SwapChain->extent.width;
    float height = (float)g_SwapChain->extent.height;

    VkViewport viewport{ 0, 0, width, height, 0, 1 };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, g_SwapChain->extent };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // RENDER PASS 2 : REFLECTION ///////////////////////////////////////////////////////////
    {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 4);

        g_Ship->UpdateReflectionUBOs(imageIndex, g_Camera, g_Sky.get());

        VkClearValue clearBlack[2];
        clearBlack[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clearBlack[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpInfo2{};
        rpInfo2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo2.renderPass = g_RenderPassReflection;
        rpInfo2.framebuffer = g_FramebufferReflection;
        rpInfo2.renderArea.extent = g_SwapChain->extent;
        rpInfo2.clearValueCount = 2;
        rpInfo2.pClearValues = clearBlack;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo2, VK_SUBPASS_CONTENTS_INLINE);
        g_Ship->RenderReflection(commandBuffer, imageIndex);
        vkCmdEndRenderPass(commandBuffer);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 5);
    }

    // COMPUTE SKY + CLOUDS (hors render pass) ///////////////////////////////////////////////////////////
    {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 6);

        switch (RENDER_SKY) {
        case 1: g_Sky->ComputeSkyImageBruneton(commandBuffer, g_iCurrentFrame, g_Camera); break;
        case 2: g_Sky->ComputeSkyImageSakmary(commandBuffer, g_iCurrentFrame, g_Camera);  break;
        }

        g_Clouds->ComputeOffScreenImage(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get(), g_Wind);

        // Barrière compute → fragment
        VkImageMemoryBarrier barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        switch (RENDER_SKY) {
        case 1: barriers[0].image = g_Sky->GetSkyImageBruneton(g_iCurrentFrame)->image; break;
        case 2: barriers[0].image = g_Sky->GetSkyImageSakmary(g_iCurrentFrame)->image;  break;
        }

        barriers[1] = barriers[0];
        barriers[1].image = g_Clouds->GetOutputImage(g_iCurrentFrame);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

        // Initialisation descriptors post (une seule fois)
        if (!g_Clouds->PostDescriptorsInitialized)
        {
            switch (RENDER_SKY) {
            case 1: g_Clouds->UpdatePostDescriptors(g_Sky->GetSkyImageBruneton(g_iCurrentFrame)); break;
            case 2: g_Clouds->UpdatePostDescriptors(g_Sky->GetSkyImageSakmary(g_iCurrentFrame));  break;
            }
            g_Clouds->PostDescriptorsInitialized = true;
        }

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 7);
    }

    // RENDER PASS BRIDGE MASK (depth+stencil only) ///////////////////////////////////
    
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame* NTT + 8);

    // === RENDER PASS BRIDGE MASK ===
    {
        VkClearValue clearDepthStencil{};
        clearDepthStencil.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo bridgeMaskPassInfo{};
        bridgeMaskPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bridgeMaskPassInfo.renderPass = g_RenderPassBridgeMask;
        bridgeMaskPassInfo.framebuffer = g_FramebufferBridgeMask;
        bridgeMaskPassInfo.renderArea.offset = { 0, 0 };
        bridgeMaskPassInfo.renderArea.extent = g_SwapChain->extent;
        bridgeMaskPassInfo.clearValueCount = 1;
        bridgeMaskPassInfo.pClearValues = &clearDepthStencil;

        // Toujours exécuter le render pass (clear stencil=0 si pas en passerelle)
        vkCmdBeginRenderPass(commandBuffer, &bridgeMaskPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        if (g_Camera.GetMode() == eCameraMode::BRIDGE && g_eBridgeView == eBridgeView::WHEEL)
        {
            g_Ship->UpdateBridgeMaskUBO(imageIndex, g_Camera);
            g_Ship->RenderOpaqueWalls(commandBuffer, imageIndex);
            g_Ship->RenderWindows(commandBuffer, imageIndex);
        }
        // Si pas en passerelle : render pass vide, stencil = 0 partout après clear
        vkCmdEndRenderPass(commandBuffer);
    }

    // === COPIE STENCIL → R8_UINT (toujours exécutée) ===
    {
        // Barrière 1 : BridgeMask DEPTH_STENCIL_READ_ONLY → TRANSFER_SRC
        VkImageMemoryBarrier barrierSrc{};
        barrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierSrc.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrierSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrierSrc.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrierSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrierSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierSrc.image = g_TexBridgeMask->image;
        barrierSrc.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierSrc);

        // Barrière 2 : R8 GENERAL → TRANSFER_DST
        VkImageMemoryBarrier barrierDst{};
        barrierDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrierDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierDst.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrierDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierDst.image = g_TexBridgeMaskR8->image;
        barrierDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierDst);

        // Copie stencil → buffer staging
        VkBufferImageCopy stencilToBuffer{};
        stencilToBuffer.bufferOffset = 0;
        stencilToBuffer.bufferRowLength = 0;
        stencilToBuffer.bufferImageHeight = 0;
        stencilToBuffer.imageSubresource = { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1 };
        stencilToBuffer.imageOffset = { 0, 0, 0 };
        stencilToBuffer.imageExtent = { g_SwapChain->extent.width, g_SwapChain->extent.height, 1 };
        vkCmdCopyImageToBuffer(commandBuffer, g_TexBridgeMask->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_BridgeMaskStagingBuffer, 1, &stencilToBuffer);

        // Barrière buffer : WRITE → READ
        VkBufferMemoryBarrier bufBarrier{};
        bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.buffer = g_BridgeMaskStagingBuffer;
        bufBarrier.offset = 0;
        bufBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &bufBarrier, 0, nullptr);

        // Copie buffer → R8_UINT
        VkBufferImageCopy bufferToR8{};
        bufferToR8.bufferOffset = 0;
        bufferToR8.bufferRowLength = 0;
        bufferToR8.bufferImageHeight = 0;
        bufferToR8.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        bufferToR8.imageOffset = { 0, 0, 0 };
        bufferToR8.imageExtent = { g_SwapChain->extent.width, g_SwapChain->extent.height, 1 };
        vkCmdCopyBufferToImage(commandBuffer, g_BridgeMaskStagingBuffer, g_TexBridgeMaskR8->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferToR8);

        // Barrière retour 1 : BridgeMask → DEPTH_STENCIL_READ_ONLY
        VkImageMemoryBarrier barrierSrcBack{};
        barrierSrcBack.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierSrcBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrierSrcBack.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrierSrcBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrierSrcBack.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrierSrcBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierSrcBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierSrcBack.image = g_TexBridgeMask->image;
        barrierSrcBack.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierSrcBack);

        // Barrière retour 2 : R8 → GENERAL (samplable)
        VkImageMemoryBarrier barrierDstBack{};
        barrierDstBack.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierDstBack.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierDstBack.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrierDstBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierDstBack.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrierDstBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierDstBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierDstBack.image = g_TexBridgeMaskR8->image;
        barrierDstBack.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierDstBack);
    }

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame* NTT + 9);

    // RENDER PASS 3 : SCÈNE MSAA ///////////////////////////////////////////////////////////

    g_Ship->UpdateUBO(commandBuffer, imageIndex, g_Camera, g_Sky.get());
    g_Axis->UpdateMsUBOs(imageIndex, g_Camera, g_Sky.get(), mat4(1.0f));
    UpdateTerrains(imageIndex);
    UpdateArrowWind(imageIndex);
    //g_Markup->Update(imageIndex, g_Camera, g_Ocean.get(), g_Sky.get());
    g_Traffics->Update(imageIndex, g_Camera, g_Sky.get());
    UpdateManoeuverZone(imageIndex);

    VkClearValue clearValues[5];
    clearValues[0].color = { {0.3f, 0.6f, 0.8f, 1.0f} };
    clearValues[1].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[2].depthStencil = { 1.0f, 0 };
    clearValues[3].depthStencil = { 1.0f, 0 };
    clearValues[4].color = { {0.0f, 0.0f, 0.0f, 1.0f} };

    VkRenderPassBeginInfo scenePassInfo{};
    scenePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    scenePassInfo.renderPass = g_RenderPassScene;
    scenePassInfo.framebuffer = g_vFramebuffersSwapChain[imageIndex];
    scenePassInfo.renderArea.offset = { 0, 0 };
    scenePassInfo.renderArea.extent = g_SwapChain->extent;
    scenePassInfo.clearValueCount = 5;
    scenePassInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(commandBuffer, &scenePassInfo, VK_SUBPASS_CONTENTS_INLINE);

    g_Clouds->RenderPost(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get());

    RenderTerrains(commandBuffer);
    g_Axis->RenderMs(commandBuffer, g_iCurrentFrame);
    g_ArrowWind->RenderMsOpaque(commandBuffer, g_iCurrentFrame);
    g_Markup->Render(commandBuffer, g_iCurrentFrame, g_Camera, g_Ocean.get(), g_Sky.get());
    g_Traffics->RenderOpaque(commandBuffer, g_iCurrentFrame);
    RenderManoeuverZone(commandBuffer);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 10);
    g_Ship->RenderOpaque(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get());
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 11);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 12);
    switch (RENDER_OCEAN) {
    case 0: g_Ocean->RenderWireframe(commandBuffer, g_iCurrentFrame, g_Camera); break;
    case 1: g_Ocean->RenderOneMesh(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get(), g_Ship->ship.Position, g_Ship->Yaw); break;
    case 2: g_Ocean->RenderInstancedMeshs(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get()); break;
    case 3:
    {
        float waveLength = glm::two_pi<float>() * g_Ship->SOG * g_Ship->SOG / 9.81f;
        float kelvinScale = 101.0f / waveLength;
        bool  bKelvinWake = g_bShipKelvinWake && (g_Ship->SOG > 0.0f);
        g_Ocean->RenderFull(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get(), g_Ship->ship.Position,
            g_Ship->Yaw, bKelvinWake, g_Ship->LWL, kelvinScale, g_Ship->SOG, g_Ship->ship.CenterFore, g_Ship->ship.BaseFroude);
    }
    break;
    }
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 13);

    g_Traffics->RenderTransparent(commandBuffer, g_iCurrentFrame);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 14);
    g_Ship->RenderTransparent(commandBuffer, g_iCurrentFrame);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 15);

    if (bAboveWater)
    {
        g_Lighthouses->RenderBeams(commandBuffer, g_iCurrentFrame, g_Camera, g_Ship->bLights);
        g_Ship->RenderSpray(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get());
        g_Ship->RenderSmoke(commandBuffer, g_iCurrentFrame, g_Camera, g_Sky.get());
        g_Lighthouses->RenderLights(commandBuffer, g_iCurrentFrame, g_Camera, g_Ship->bLights);
        g_Markup->RenderLights(commandBuffer, g_iCurrentFrame, g_Camera, g_Ship->bLights);
        g_Traffics->RenderLights(commandBuffer, g_iCurrentFrame, g_Camera, g_Ship->bLights);
        g_Ship->RenderNavLights(commandBuffer, g_Camera);
    }

    RenderTextures(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    // RENDER PASS 4 : POST-PROCESS ///////////////////////////////////////////////////////////
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 16);

    ScreenQuad::sPostProcessingUBO ubo;
    ubo.exposure = g_Sky->Exposure;
    ubo.zNear = 0.1f;
    ubo.zFar = 30000.f;
    ubo.horizonHeight = g_Camera.GetHorizonViewportY();
    ubo.eyePos = g_Camera.GetPosition();
    ubo.oceanColor = g_Ocean->OceanColor;
    ubo.fogColor = g_Sky->FogColor;
    ubo.bOcean = (int)g_Ocean->bVisible;
    ubo.fogDensity = g_Sky->FogDensity;
    ubo.uTime = g_TimerScene.getTime();
    ubo.screenSize = vec2(width, height);
    ubo.bLowIntensity = (int)g_bLowIntensity;
    ubo.bNightVision = (int)g_bNightVision;
    ubo.sunDirection = glm::normalize(g_Sky->SunDirection);
    ubo.mieExponent = 8.0f;
    ubo.sunColor = g_Sky->SunDiffuse;
    ubo.bBinoculars = (int)g_bBinoculars;
    ubo.bRainDropsTrails = (int)g_Sky->bRain;
    ubo.bRainBlurDrips = 0;
    ubo.bInside = (int)(g_Camera.GetMode() == eCameraMode::BRIDGE && g_eBridgeView == eBridgeView::WHEEL);
    g_ScreenQuad->UpdateUbo(g_iCurrentFrame, ubo);

    VkRenderPassBeginInfo postPassInfo{};
    postPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    postPassInfo.renderPass = g_RenderPassPostProcess;
    postPassInfo.framebuffer = g_vFramebuffersPostProcess[imageIndex];
    postPassInfo.renderArea.offset = { 0, 0 };
    postPassInfo.renderArea.extent = g_SwapChain->extent;
    postPassInfo.clearValueCount = 0;
    postPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(commandBuffer, &postPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    g_ScreenQuad->Render(commandBuffer, g_iCurrentFrame);
    vkCmdEndRenderPass(commandBuffer);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_QueryPool, g_iCurrentFrame * NTT + 17);

    // RENDER PASS 5 : ImGui ///////////////////////////////////////////////////////////
    VkRenderPassBeginInfo imguiPassInfo{};
    imguiPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    imguiPassInfo.renderPass = g_RenderPassImGui;
    imguiPassInfo.framebuffer = g_vFramebuffersImGui[imageIndex];
    imguiPassInfo.renderArea.offset = { 0, 0 };
    imguiPassInfo.renderArea.extent = g_SwapChain->extent;
    imguiPassInfo.clearValueCount = 0;
    imguiPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(commandBuffer, &imguiPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    FrameImGui(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    if (!bSaved)
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(g_Device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_Device->graphicsQueue);

        SaveDepthTexture2D(g_Device, g_TexShadowDepth->image, g_ShadowWidth, g_ShadowHeight, "Outputs/vulkan.png");
        bSaved = true;
    }
}
void GetTimestamps(uint32_t imageIndex)
{
    auto now = chrono::high_resolution_clock::now();

    if (g_FirstRun)
    {
        g_LastUpdateTime = now;
        g_FirstRun = false;
    }

    // Check if 1 second has elapsed
    auto elapsed = chrono::duration_cast<chrono::seconds>(now - g_LastUpdateTime);
    bool shouldUpdate = (elapsed.count() >= 1);

    // Offset for the current frame (2 frames = 20 timestamps in total)
    uint32_t frameOffset = imageIndex * NTT;

    uint64_t mTimestamps[NTT];
    // Read only the timestamps of the current frame
    VkResult result = vkGetQueryPoolResults(g_Device->device, g_QueryPool, frameOffset, NTT, NTT * sizeof(uint64_t), mTimestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result != VK_SUCCESS) return;
    
    const char* names[] = { "Shadow", "Reflection", "Wake", "Sky + Clouds", "Bridge masks", "Ship Opaque", "Ocean", "Ship Transp", "Post process", "= RENDER =" };

    // 4 pairs start/end (index 0-1, 2-3, 4-5, 6-7, 8-9)
    for (int i = 0; i < NCHRONOS_1; i++)
    {
        uint64_t start = mTimestamps[2 * i];
        uint64_t end = mTimestamps[2 * i + 1];

        // 1. No 64-bit overflow
        if (end < start && (start - end) >(1ULL << 62)) continue;

        // 2. Reasonable delta (< 100ms)
        uint64_t delta = (end >= start) ? (end - start) : 0;
        if (delta > (100000000ULL / g_Device->timestampPeriod)) continue; // >100ms

        uint64_t ns = uint64_t(double(delta) * g_Device->timestampPeriod);

        // Accumulate
        if (ns >= 100 && ns <= 50000000)
        {
            g_TimestampAccumulators[i] += ns;
            g_TimestampCounts[i]++;
        }

        // Update only if 1 second has elapsed
        if (shouldUpdate)
        {
            double averageNs = static_cast<double>(g_TimestampAccumulators[i]) / g_TimestampCounts[i];
            g_SceneTimeStamps[i] = make_pair(string(names[i]), static_cast<uint64_t>(averageNs));

            // Reset accumulators
            g_TimestampAccumulators[i] = 0;
            g_TimestampCounts[i] = 0;
        }
    }

    // Total frame (timestamp 0 to 9)
    uint64_t deltaTicks = mTimestamps[NTT - 1] - mTimestamps[0];
    uint64_t ns = uint64_t(double(deltaTicks) * g_Device->timestampPeriod);
    g_TimestampAccumulators[NCHRONOS_1] += ns;
    g_TimestampCounts[NCHRONOS_1]++;
    if (shouldUpdate)
    {
        double averageNs = static_cast<double>(g_TimestampAccumulators[NCHRONOS_1]) / g_TimestampCounts[NCHRONOS_1];
        g_SceneTimeStamps[NCHRONOS_1] = make_pair(string(names[NCHRONOS_1]), static_cast<uint64_t>(averageNs));

        // Reset accumulators
        g_TimestampAccumulators[NCHRONOS_1] = 0;
        g_TimestampCounts[NCHRONOS_1] = 0;
    }

    // Update the reference time after processing
    if (shouldUpdate)
        g_LastUpdateTime = now;
}
void Render()
{
    static bool bInit = false;
    static float firstTime = g_TimerScene.getTime();
    if (g_bReset)
    {
        bInit = false;
        firstTime = g_TimerScene.getTime();
        g_Ship->bMotion = false;
        g_bReset = false;
    }

    if (!bInit && (g_TimerScene.getTime() > firstTime + 1.0f))
    {
        g_Ship->bMotion = true;
        bInit = true;
    }

    // Wait for the current frame's fence before reusing its resources
    vkWaitForFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame], VK_TRUE, UINT64_MAX);

    // Acquire the next swapchain image; signal imageAvailable[currentFrame] when ready
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(g_Device->device, g_SwapChain->swapChain, UINT64_MAX, g_vImageAvailableSemaphores[g_iCurrentFrame],   // indexed on currentFrame
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return; // Handle swapchain recreation here if needed

    // Reset fence and command buffer only once we know this frame will be submitted
    vkResetFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame]);
    vkResetCommandBuffer(g_vCommandBuffers[g_iCurrentFrame], 0);

    // Record all rendering commands for this frame
    FrameRender(g_vCommandBuffers[g_iCurrentFrame], imageIndex);

    // ── Build wait semaphore list ────────────────────────────────────────
    VkSemaphore          waitSemaphores[3];
    VkPipelineStageFlags waitStages[3];
    uint32_t             waitCount = 0;

    // Always wait for the swapchain image to be available
    waitSemaphores[waitCount] = g_vImageAvailableSemaphores[g_iCurrentFrame];
    waitStages[waitCount] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    waitCount++;

    // Wait for ocean compute only if it was submitted this frame
    if (g_Ocean && g_Ocean->ComputeWasPending)
    {
        waitSemaphores[waitCount] = g_Ocean->ComputeFinishedSem[g_iCurrentFrame];
        waitStages[waitCount] = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        waitCount++;
    }

    // ── Submit graphics command buffer ───────────────────────────────────
    
    // Signal semaphore indexed on currentFrame (NOT imageIndex) to avoid signaling a semaphore still pending from a previous present cycle
    VkSemaphore signalSemaphores[] = { g_vRenderFinishedSemaphores[g_iCurrentFrame] };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = waitCount;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_vCommandBuffers[g_iCurrentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // Fence signals when this frame's GPU work is complete
    vkQueueSubmit(g_Device->graphicsQueue, 1, &submitInfo, g_vImageFences[g_iCurrentFrame]);

    // ── Present ──────────────────────────────────────────────────────────
    
    // Wait on the same renderFinished[currentFrame] semaphore before presenting
    VkSwapchainKHR swapchains[] = { g_SwapChain->swapChain };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;  // currentFrame ✅
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(g_Device->presentQueue, &presentInfo);

    // ── Timestamps & FPS counter ─────────────────────────────────────────
    GetTimestamps(g_iCurrentFrame);

    g_FpsFrameCount++;
    double currentTime = glfwGetTime();
    if (currentTime - g_FpsTime >= 1.0)
    {
        g_nFps = static_cast<int>(g_FpsFrameCount / (currentTime - g_FpsTime));
        g_FpsFrameCount = 0;
        g_FpsTime = currentTime;
    }

    // Advance to the next frame slot (wraps around MAX_FRAMES_IN_FLIGHT)
    g_iCurrentFrame = (g_iCurrentFrame + 1) % g_vImageFences.size();
}

// Initialization
void InitConsole()
{
    // Create a Windows console attached to the process

    // UTF-8 codepage for Windows console
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    // Force UTF-16 for streams
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    // Locales + sync
    ios::sync_with_stdio(false);
    locale::global(locale(".65001"));  // UTF-8 locale

    // Fix wcout specific to Windows
    wcout.imbue(locale(".65001"));
    wcerr.imbue(locale(".65001"));

    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);

    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    SetConsoleTitle(L"SimShip Console");
    SetWindowPos(GetConsoleWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE);
}
void InitWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    g_hWindow = glfwCreateWindow(g_WindowW, g_WindowH, "SimShip (Vulkan)", nullptr, nullptr);
    if (!g_hWindow)
        throw runtime_error("Failed to create GLFW window");

    // Center the window on the primary monitor
    g_Monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* videoMode = glfwGetVideoMode(g_Monitor);
    glfwSetWindowPos(g_hWindow, (videoMode->width - g_WindowW) / 2, (videoMode->height - g_WindowH) / 2);

    glfwSetFramebufferSizeCallback(g_hWindow, framebuffer_size_callback);
    glfwSetCursorPosCallback(g_hWindow, CursorPosCallback);
    glfwSetScrollCallback(g_hWindow, ScrollCallback);
    glfwSetMouseButtonCallback(g_hWindow, MouseButtonCallback);
    glfwSetKeyCallback(g_hWindow, KeyCallback);
    glfwShowWindow(g_hWindow);
}
void CreateInstance()
{
    // Create a Vulkan instance (the global context)

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 4, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 4, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Validation layers
    if (g_bEnableValidationLayers) 
    {
        if (!checkValidationLayerSupport()) 
            throw runtime_error("Validation layers are not available!");

        createInfo.enabledLayerCount = static_cast<uint32_t>(g_vValidationLayers.size());
        createInfo.ppEnabledLayerNames = g_vValidationLayers.data();
#ifdef INFO_INIT    // Structures.h
        wcout << L"Validation layers enabled\n";
#endif
    }
    else
        createInfo.enabledLayerCount = 0;

    // Extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (g_bEnableValidationLayers)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &g_Instance) != VK_SUCCESS)
        throw runtime_error("Vulkan instance creation failed");

    if (g_bEnableValidationLayers) 
        setupDebugMessenger(g_Instance);

#ifdef INFO_INIT    // Structures.h
    wcout << L"Instance OK\n";
#endif
}

void CreateRenderPassImGuiLoading()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = g_SwapChain->imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;      // ← CLEAR here
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &colorAttachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    vkCreateRenderPass(g_Device->device, &info, nullptr, &g_RenderPassImGuiLoading);
}
void CreateRenderPassShadow()
{
	// ATTACHMENT 0 : DEPTH only
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();             // VK_FORMAT_D32_SFLOAT
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;        // Non multisample
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // Clear depth at beginning of the render pass
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // We will read from depth, so it's important to store the depth attachment results
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;// Attachment will be transitioned to shader read at render pass end

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment will be used as depth / stencil during render pass

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;                       // No color attachments
    subpass.pDepthStencilAttachment = &depthAttachmentRef;  // Reference to our depth attachment

    // Use subpass dependencies for layout transitions
    array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    vkCreateRenderPass(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassShadow);
}
void CreateRenderPassReflection()
{
    VkAttachmentDescription attachments[2] = {};

    // ATTACHMENT 0 : COLOR
    attachments[0].format = g_SwapChain->imageFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;                         // Non multisample
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;  // Ocean sampling

    // ATTACHMENT 1 : DEPTH
    attachments[1].format = FindDepthFormat();                              // VK_FORMAT_D32_SFLOAT
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;              // Depth temporaire
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // References
    VkAttachmentReference colorAttachmentRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthAttachmentRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;  // ← 2 attachments
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    vkCreateRenderPass(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassReflection);
}
void CreateRenderPassWake()
{
    VkAttachmentDescription attachments[1] = {};

    // ATTACHMENT 0 : COLOR
    attachments[0].format = VK_FORMAT_R8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;                         // Non multisample
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;  // Ocean sampling

    // References
    VkAttachmentReference colorAttachmentRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo wakePassInfo{};
    wakePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    wakePassInfo.attachmentCount = 1;  // ← 2 attachments
    wakePassInfo.pAttachments = attachments;
    wakePassInfo.subpassCount = 1;
    wakePassInfo.pSubpasses = &subpass;
    vkCreateRenderPass(g_Device->device, &wakePassInfo, nullptr, &g_RenderPassWake);
}
void CreateRenderPassScene()
{
    VkFormat colorFormat = g_SwapChain->imageFormat;
    VkFormat depthFormat = FindDepthFormat();

    // ATTACHMENT 0 : Color MSAA (rendu)
    VkAttachmentDescription2 colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    colorAttachment.format = colorFormat;
    colorAttachment.samples = g_Device->msaaSamples;  // 8x
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // ATTACHMENT 1 : Color Resolve Swapchain (présenté)
    VkAttachmentDescription2 colorSwapchainAttachment{};
    colorSwapchainAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    colorSwapchainAttachment.format = colorFormat;
    colorSwapchainAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorSwapchainAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorSwapchainAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorSwapchainAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorSwapchainAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorSwapchainAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorSwapchainAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // ATTACHMENT 2 : Depth MSAA (rendu)
    VkAttachmentDescription2 depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depthAttachment.format = depthFormat;
    depthAttachment.samples = g_Device->msaaSamples;  // 8x
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ATTACHMENT 3 : Depth Resolve 1x (pour ScreenQuad)
    VkAttachmentDescription2 depthResolveAttachment{};
    depthResolveAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depthResolveAttachment.format = depthFormat;
    depthResolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthResolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthResolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthResolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthResolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthResolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthResolveAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    // ATTACHMENT 4 : Color Resolve 1x (pour ScreenQuad)
    VkAttachmentDescription2 colorResolveAttachment{};
    colorResolveAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    colorResolveAttachment.format = colorFormat;
    colorResolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorResolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorResolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorResolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorResolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorResolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorResolveAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    array<VkAttachmentDescription2, 5> attachments = {
        colorAttachment,            // 0: MSAA color
        colorSwapchainAttachment,   // 1: Swapchain
        depthAttachment,            // 2: MSAA depth
        depthResolveAttachment,     // 3: 1x depth
        colorResolveAttachment      // 4: 1x color
    };

    // References
    VkAttachmentReference2 colorAttachmentRef{};
    colorAttachmentRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 colorSwapchainRef{};
    colorSwapchainRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    colorSwapchainRef.attachment = 1;
    colorSwapchainRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 depthAttachmentRef{};
    depthAttachmentRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depthAttachmentRef.attachment = 2;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 depthResolveRef{};
    depthResolveRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depthResolveRef.attachment = 3;
    depthResolveRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 colorResolveRef{};
    colorResolveRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    colorResolveRef.attachment = 4;
    colorResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // DepthStencilResolve
    VkSubpassDescriptionDepthStencilResolve depthStencilResolve{};
    depthStencilResolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
    depthStencilResolve.pDepthStencilResolveAttachment = &depthResolveRef;
    depthStencilResolve.depthResolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    depthStencilResolve.stencilResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

    // Subpass (1 seul)
    VkSubpassDescription2 subpass{};
    subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;         // att. 0 MSAA
    subpass.pResolveAttachments = &colorResolveRef;          // att. 4 RESOLVE 1x
    subpass.pDepthStencilAttachment = &depthAttachmentRef;   // att. 2 MSAA
    subpass.pNext = &depthStencilResolve;                    // att. 3 depth resolve

    // RenderPassCreate
    VkRenderPassCreateInfo2 renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    renderPassInfo.attachmentCount = 5;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    vkCreateRenderPass2(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassScene);
}
void CreateRenderPassPostProcess()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = g_SwapChain->imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = nullptr;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 0;
    renderPassInfo.pDependencies = nullptr;

    vkCreateRenderPass(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassPostProcess);
}
void CreateRenderPassImGui()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = g_SwapChain->imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    vkCreateRenderPass(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassImGui);
}
void CreateRenderPassBridgeMask()
{
    // ATTACHMENT 0 : DEPTH + STENCIL only (no color)
    VkAttachmentDescription depthStencilAttachment{};
    depthStencilAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
    depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthStencilRef{};
    depthStencilRef.attachment = 0;
    depthStencilRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthStencilRef;

    // Dependencies: same pattern as CreateRenderPassShadow
    // dep[0]: wait for the previous fragment shader to finish reading before writing to depth/stencil
    // dep[1]: wait for depth/stencil write to finish before the next fragment shader can read
    array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthStencilAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    vkCreateRenderPass(g_Device->device, &renderPassInfo, nullptr, &g_RenderPassBridgeMask);
}

void CreateImages()
{
	// Images used in the framebuffers

	// Color 8x MSAA
    g_ColorImage = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, g_Device->msaaSamples, g_SwapChain->imageFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    
	// Depth 8x MSAA
    g_DepthImage = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, g_Device->msaaSamples, FindDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

	// Color Resolve 1x
    g_ColorImageResolve = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, g_SwapChain->imageFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	// Depth Resolve 1x
    g_DepthImageResolve = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, FindDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Textures for shaders

	// Reflection textures
    g_TexReflectionColor = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, g_SwapChain->imageFormat, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    g_TexReflectionDepth = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, FindDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

	// Shadow map texture
    g_TexShadowDepth = make_unique<VulkanTexture>(
        g_Device, g_ShadowWidth, g_ShadowHeight, 1,
        1, VK_SAMPLE_COUNT_1_BIT, FindDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.compareEnable = VK_TRUE;
    sampler.compareOp = VK_COMPARE_OP_LESS;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.mipLodBias = 0.0f;
    sampler.anisotropyEnable = VK_FALSE;
    sampler.maxAnisotropy = 1.0f;
    sampler.minLod = 0.0f;
    sampler.maxLod = 1.0f;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(g_Device->device, &sampler, nullptr, &g_TexShadowDepthSampler);

	// Wake texture
    g_TexWake0 = make_unique<VulkanTexture>(
        g_Device, g_WakeSize, g_WakeSize, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    g_TexWake1 = make_unique<VulkanTexture>(
        g_Device, g_WakeSize, g_WakeSize, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    g_TexWake2 = make_unique<VulkanTexture>(
        g_Device, g_WakeSize, g_WakeSize, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	// Transition wake textures to GENERAL layout for compute shaders
    g_TexWake0->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    g_TexWake1->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    g_TexWake2->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Bridge mask depth stencil
    g_TexBridgeMask = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    
    g_TexBridgeMask->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    // Texture R8_UINT to receive the copied stencil
    g_TexBridgeMaskR8 = make_unique<VulkanTexture>(
        g_Device, g_SwapChain->extent.width, g_SwapChain->extent.height, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT  | VK_IMAGE_USAGE_SAMPLED_BIT,               // reçoit la copie + samplable en shader
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    g_TexBridgeMaskR8->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_FALSE;
    vkCreateSampler(g_Device->device, &samplerInfo, nullptr, &g_TexBridgeMaskSampler);
}

void CreateFramebufferShadow()
{
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = g_RenderPassShadow;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &g_TexShadowDepth->imageView;
    framebufferInfo.width = g_ShadowWidth;
    framebufferInfo.height = g_ShadowHeight;
    framebufferInfo.layers = 1;
    vkCreateFramebuffer(g_Device->device, &framebufferInfo, nullptr, &g_FramebufferShadow);
}
void CreateFramebufferReflection()
{
    VkImageView attachments[] = { g_TexReflectionColor->imageView, g_TexReflectionDepth->imageView };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = g_RenderPassReflection;
    fbInfo.attachmentCount = 2;  // ← COLOR + DEPTH
    fbInfo.pAttachments = attachments;
    fbInfo.width = g_SwapChain->extent.width;
    fbInfo.height = g_SwapChain->extent.height;
    fbInfo.layers = 1;
    vkCreateFramebuffer(g_Device->device, &fbInfo, nullptr, &g_FramebufferReflection);
}
void CreateFramebufferWake()
{
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = g_RenderPassWake;
    fbInfo.attachmentCount = 1; // ← COLOR
    fbInfo.pAttachments = &g_TexWake0->imageView;
    fbInfo.width = g_WakeSize;
    fbInfo.height = g_WakeSize;
    fbInfo.layers = 1;
    vkCreateFramebuffer(g_Device->device, &fbInfo, nullptr, &g_FramebufferWake);
}
void CreateFramebuffersScene()
{
    g_vFramebuffersSwapChain.resize(g_SwapChain->imageCount);
    
    for (size_t i = 0; i < g_SwapChain->imageCount; i++)
    {
        // 5 attachements
        array<VkImageView, 5> attachments = {
            g_ColorImage->imageView,                // 0: MSAA color
            g_SwapChain->vImageViews[i],            // 1: swapchain
            g_DepthImage->imageView,                // 2: MSAA depth
            g_DepthImageResolve->imageView,         // 3: 1x depth resolve
            g_ColorImageResolve->imageView          // 4: 1x color resolve
        };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = g_RenderPassScene;
        framebufferInfo.attachmentCount = 5; 
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = g_SwapChain->extent.width;
        framebufferInfo.height = g_SwapChain->extent.height;
        framebufferInfo.layers = 1;

        vkCreateFramebuffer(g_Device->device, &framebufferInfo, nullptr, &g_vFramebuffersSwapChain[i]);
    }
}
void CreateFramebuffersPostProcess()
{
    g_vFramebuffersPostProcess.resize(g_SwapChain->imageCount);

    for (size_t i = 0; i < g_SwapChain->imageCount; i++)
    {
        // 1 attachment only — like the original
        VkImageView attachments[] = {
            g_SwapChain->vImageViews[i]     // ← color only
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = g_RenderPassPostProcess;
        framebufferInfo.attachmentCount = 1;    // ← 1 instead of 2
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = g_SwapChain->extent.width;
        framebufferInfo.height = g_SwapChain->extent.height;
        framebufferInfo.layers = 1;

        vkCreateFramebuffer(g_Device->device, &framebufferInfo, nullptr, &g_vFramebuffersPostProcess[i]);
    }
}
void CreateFramebufferBridgeMask()
{
    // DEPTH+STENCIL view for the framebuffer (write)
    VkImageViewCreateInfo fbViewInfo{};
    fbViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    fbViewInfo.image = g_TexBridgeMask->image;
    fbViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    fbViewInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
    fbViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; // ← both
    fbViewInfo.subresourceRange.baseMipLevel = 0;
    fbViewInfo.subresourceRange.levelCount = 1;
    fbViewInfo.subresourceRange.baseArrayLayer = 0;
    fbViewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(g_Device->device, &fbViewInfo, nullptr, &g_TexBridgeMaskFramebufferView);

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = g_RenderPassBridgeMask;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &g_TexBridgeMaskFramebufferView;
    fbInfo.width = g_SwapChain->extent.width;
    fbInfo.height = g_SwapChain->extent.height;
    fbInfo.layers = 1;
    vkCreateFramebuffer(g_Device->device, &fbInfo, nullptr, &g_FramebufferBridgeMask);
}
void CreateBridgeMaskStagingBuffer()
{
    VkDeviceSize bufferSize = g_SwapChain->extent.width * g_SwapChain->extent.height; // 1 byte/pixel stencil

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g_Device->device, &bufInfo, nullptr, &g_BridgeMaskStagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(g_Device->device, g_BridgeMaskStagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(g_Device->physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);   // ← GPU only, pas besoin CPU
    vkAllocateMemory(g_Device->device, &allocInfo, nullptr, &g_BridgeMaskStagingMemory);
    vkBindBufferMemory(g_Device->device, g_BridgeMaskStagingBuffer, g_BridgeMaskStagingMemory, 0);
}
void CreateFramebuffersImGui()
{
    g_vFramebuffersImGui.resize(g_SwapChain->imageCount);

    for (size_t i = 0; i < g_SwapChain->imageCount; i++)
    {
        // 1 attachment only: color swapchain only
        VkImageView attachments[] = {
            g_SwapChain->vImageViews[i]     // 0: color only
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = g_RenderPassImGui;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = g_SwapChain->extent.width;
        framebufferInfo.height = g_SwapChain->extent.height;
        framebufferInfo.layers = 1;

        vkCreateFramebuffer(g_Device->device, &framebufferInfo, nullptr, &g_vFramebuffersImGui[i]);
    }
}

void CreateCommandBuffers()
{
    uint32_t framebufferCount = static_cast<uint32_t>(g_vFramebuffersSwapChain.size());
    if (framebufferCount == 0)
        throw runtime_error("No framebuffer!");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_Device->graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = framebufferCount;

    g_vCommandBuffers.resize(framebufferCount);
    if (vkAllocateCommandBuffers(g_Device->device, &allocInfo, g_vCommandBuffers.data()) != VK_SUCCESS) 
        throw runtime_error("Failed to allocate command buffers");

#ifdef INFO_INIT    // Structures.h
    wcout << framebufferCount << L" command buffers OK\n";
#endif // INFO_INIT
}
void CreateSyncObjects()
{
    uint32_t imageCount = static_cast<uint32_t>(g_SwapChain->imageCount);  // 2

    g_vImageAvailableSemaphores.resize(imageCount);
    g_vRenderFinishedSemaphores.resize(imageCount);
    g_vImageFences.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < imageCount; i++) 
    {  
        vkCreateSemaphore(g_Device->device, &semaphoreInfo, nullptr, &g_vImageAvailableSemaphores[i]);
        vkCreateSemaphore(g_Device->device, &semaphoreInfo, nullptr, &g_vRenderFinishedSemaphores[i]);
        vkCreateFence(g_Device->device, &fenceInfo, nullptr, &g_vImageFences[i]);
    }

#ifdef INFO_INIT    // Structures.h
    wcout << imageCount << L" sync sets (1 par image) OK\n";
#endif // INFO_INIT
}
void CreateQueryPool()
{
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = NTT * g_SwapChain->imageCount; // 2 timestamps per image
    
    if (vkCreateQueryPool(g_Device->device, &queryPoolInfo, nullptr, &g_QueryPool) != VK_SUCCESS)
		throw runtime_error("Échec création query pool");
}

void InitVulkan()
{
    int width, height;
    glfwGetFramebufferSize(g_hWindow, &width, &height);
    g_SwapChain = make_unique<VulkanSwapChain>(g_Device, width, height);
    g_FramesInFlight = g_SwapChain->imageCount;

    // RenderPass (defines the expected formats)
    CreateRenderPassImGui();
    CreateRenderPassShadow();           // Shadow
    CreateRenderPassReflection();       // Reflection
	CreateRenderPassWake();             // Wake 
    CreateRenderPassScene();            // MSAA Scene
    CreateRenderPassPostProcess();      // Post processing
    CreateRenderPassImGuiLoading();
    CreateRenderPassBridgeMask();

    // Create all images + their individual views
    CreateImages();
   
    // Framebuffers (uses all views)
    CreateFramebufferShadow();          // Render pass 0 (Shadow)
    CreateFramebufferReflection();      // Render pass 1 (Reflection)
	CreateFramebufferWake();            // Render pass 2 (Wake)
    CreateFramebuffersScene();          // Render pass 3 (3D)
    CreateFramebuffersPostProcess();    // Render pass 4 (2D)
    CreateFramebufferBridgeMask();
    CreateBridgeMaskStagingBuffer();
    CreateFramebuffersImGui();

    CreateCommandBuffers();
    CreateSyncObjects();
    CreateQueryPool();
}
void ApplyTheme()
{
    if (IMGUI_STYLE > 4)
		IMGUI_STYLE = 0;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    switch (IMGUI_STYLE)
    {
    case 0: ImGui::StyleColorsDark(); break;
    case 1: // Nord
    {
        // Corners
        style.WindowRounding = 0.0f;

        // Borders
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;   // 0 border on frames (like Charcoal)
        style.PopupBorderSize = 1.0f;

        // Padding and spacing
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(5.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 21.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 10.0f;

        // Nord palette
        const ImVec4 polarNight0 = ImVec4(0.16f, 0.18f, 0.22f, 1.00f); // #2e3440
        const ImVec4 polarNight1 = ImVec4(0.18f, 0.20f, 0.25f, 1.00f); // #3b4252
        const ImVec4 polarNight2 = ImVec4(0.20f, 0.23f, 0.29f, 1.00f); // #434c5e
        const ImVec4 polarNight3 = ImVec4(0.23f, 0.26f, 0.32f, 1.00f); // #4c566a
        const ImVec4 snowStorm1 = ImVec4(0.85f, 0.87f, 0.91f, 1.00f); // #d8dee9
        const ImVec4 snowStorm2 = ImVec4(0.90f, 0.91f, 0.94f, 1.00f); // #e5e9f0
        const ImVec4 snowStorm3 = ImVec4(0.93f, 0.95f, 0.97f, 1.00f); // #eceff4
        const ImVec4 frost1 = ImVec4(0.55f, 0.70f, 0.79f, 1.00f); // #8fbcbb
        const ImVec4 frost2 = ImVec4(0.53f, 0.75f, 0.82f, 1.00f); // #88c0d0
        const ImVec4 frost3 = ImVec4(0.37f, 0.51f, 0.67f, 1.00f); // #81a1c1
        const ImVec4 frost4 = ImVec4(0.36f, 0.51f, 0.72f, 1.00f); // #5e81ac
        const ImVec4 auroraRed = ImVec4(0.75f, 0.38f, 0.42f, 1.00f); // #bf616a
        const ImVec4 auroraOrange = ImVec4(0.82f, 0.53f, 0.39f, 1.00f); // #d08770
        const ImVec4 auroraYellow = ImVec4(0.92f, 0.80f, 0.55f, 1.00f); // #ebcb8b
        const ImVec4 auroraGreen = ImVec4(0.64f, 0.75f, 0.55f, 1.00f); // #a3be8c
        const ImVec4 auroraPurple = ImVec4(0.71f, 0.56f, 0.74f, 1.00f); // #b48ead
        const ImVec4 border = ImVec4(0.27f, 0.30f, 0.35f, 1.00f);

        colors[ImGuiCol_Text] = snowStorm3;
        colors[ImGuiCol_TextDisabled] = snowStorm1;
        colors[ImGuiCol_WindowBg] = polarNight1;
        colors[ImGuiCol_ChildBg] = polarNight2;
        colors[ImGuiCol_PopupBg] = polarNight2;
        colors[ImGuiCol_Border] = border;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = polarNight3;
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.35f, 0.42f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = frost3;
        colors[ImGuiCol_TitleBg] = polarNight0;
        colors[ImGuiCol_TitleBgActive] = frost3;
        colors[ImGuiCol_TitleBgCollapsed] = polarNight1;
        colors[ImGuiCol_MenuBarBg] = polarNight2;
        colors[ImGuiCol_ScrollbarBg] = polarNight0;
        colors[ImGuiCol_ScrollbarGrab] = frost3;
        colors[ImGuiCol_ScrollbarGrabHovered] = frost2;
        colors[ImGuiCol_ScrollbarGrabActive] = frost2;
        colors[ImGuiCol_CheckMark] = auroraGreen;
        colors[ImGuiCol_SliderGrab] = frost4;
        colors[ImGuiCol_SliderGrabActive] = frost2;
        colors[ImGuiCol_Button] = polarNight3;
        colors[ImGuiCol_ButtonHovered] = frost3;
        colors[ImGuiCol_ButtonActive] = frost2;
        colors[ImGuiCol_Header] = ImVec4(0.37f, 0.51f, 0.67f, 0.40f);
        colors[ImGuiCol_HeaderHovered] = frost3;
        colors[ImGuiCol_HeaderActive] = frost2;
        colors[ImGuiCol_Separator] = border;
        colors[ImGuiCol_SeparatorHovered] = auroraPurple;
        colors[ImGuiCol_SeparatorActive] = auroraPurple;
        colors[ImGuiCol_ResizeGrip] = frost3;
        colors[ImGuiCol_ResizeGripHovered] = frost2;
        colors[ImGuiCol_ResizeGripActive] = frost2;
        colors[ImGuiCol_Tab] = polarNight3;
        colors[ImGuiCol_TabHovered] = frost2;
        colors[ImGuiCol_TabActive] = frost2;
        colors[ImGuiCol_TabUnfocused] = polarNight1;
        colors[ImGuiCol_TabUnfocusedActive] = polarNight3;
        colors[ImGuiCol_PlotLines] = frost2;
        colors[ImGuiCol_PlotLinesHovered] = auroraOrange;
        colors[ImGuiCol_PlotHistogram] = frost3;
        colors[ImGuiCol_PlotHistogramHovered] = auroraGreen;
        colors[ImGuiCol_TableHeaderBg] = polarNight3;
        colors[ImGuiCol_TableBorderStrong] = border;
        colors[ImGuiCol_TableBorderLight] = polarNight3;
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.51f, 0.67f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = auroraYellow;
        colors[ImGuiCol_NavHighlight] = frost1;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.85f, 0.90f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    }
    break;
    case 2: // Carbon
    {
        // Corners
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;

        // Borders
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;

        // Padding and spacing
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(5.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 21.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 10.0f;

        // Carbon palette
        const ImVec4 bg0 = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // darkest background
        const ImVec4 bg1 = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // bgVeryDark
        const ImVec4 bg2 = ImVec4(0.16f, 0.16f, 0.16f, 1.00f); // bgDark
        const ImVec4 bg3 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f); // bgLight (frame bg)
        const ImVec4 bg4 = ImVec4(0.27f, 0.27f, 0.27f, 1.00f); // frame bg hover discreet
        const ImVec4 overlay0 = ImVec4(0.35f, 0.35f, 0.35f, 1.00f); // overlay discreet
        const ImVec4 overlay1 = ImVec4(0.45f, 0.45f, 0.45f, 1.00f); // overlay medium
        const ImVec4 textLight = ImVec4(0.96f, 0.96f, 0.96f, 1.00f); // main text
        const ImVec4 textDisabled = ImVec4(0.55f, 0.55f, 0.55f, 1.00f); // disabled text
        const ImVec4 accentDark = ImVec4(0.20f, 0.48f, 0.65f, 1.00f); // dark cyan-blue
        const ImVec4 accent = ImVec4(0.30f, 0.60f, 0.80f, 1.00f); // main cyan-blue
        const ImVec4 accentLight = ImVec4(0.40f, 0.65f, 0.85f, 1.00f); // light cyan-blue
        const ImVec4 accentBright = ImVec4(0.55f, 0.75f, 0.92f, 1.00f); // bright cyan-blue
        const ImVec4 cyan = ImVec4(0.30f, 0.80f, 0.85f, 1.00f); // complementary cyan
        const ImVec4 yellow = ImVec4(0.90f, 0.80f, 0.35f, 1.00f); // drag&drop yellow
        const ImVec4 orange = ImVec4(0.85f, 0.55f, 0.25f, 1.00f); // orange plot hover
        const ImVec4 purple = ImVec4(0.60f, 0.45f, 0.80f, 1.00f); // separator hover
        const ImVec4 green = ImVec4(0.45f, 0.80f, 0.45f, 1.00f); // checkmark
        const ImVec4 border = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);

        colors[ImGuiCol_Text] = textLight;
        colors[ImGuiCol_TextDisabled] = textDisabled;
        colors[ImGuiCol_WindowBg] = bg1;
        colors[ImGuiCol_ChildBg] = bg2;
        colors[ImGuiCol_PopupBg] = bg2;
        colors[ImGuiCol_Border] = border;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = bg3;
        colors[ImGuiCol_FrameBgHovered] = bg4;
        colors[ImGuiCol_FrameBgActive] = overlay0;
        colors[ImGuiCol_TitleBg] = bg0;
        colors[ImGuiCol_TitleBgActive] = accent;
        colors[ImGuiCol_TitleBgCollapsed] = bg1;
        colors[ImGuiCol_MenuBarBg] = bg2;
        colors[ImGuiCol_ScrollbarBg] = bg0;
        colors[ImGuiCol_ScrollbarGrab] = accent;
        colors[ImGuiCol_ScrollbarGrabHovered] = accentLight;
        colors[ImGuiCol_ScrollbarGrabActive] = accentBright;
        colors[ImGuiCol_CheckMark] = green;
        colors[ImGuiCol_SliderGrab] = accentDark;
        colors[ImGuiCol_SliderGrabActive] = accent;
        colors[ImGuiCol_Button] = bg3;
        colors[ImGuiCol_ButtonHovered] = accent;
        colors[ImGuiCol_ButtonActive] = accentLight;
        colors[ImGuiCol_Header] = ImVec4(0.30f, 0.60f, 0.80f, 0.40f);
        colors[ImGuiCol_HeaderHovered] = accent;
        colors[ImGuiCol_HeaderActive] = accentLight;
        colors[ImGuiCol_Separator] = border;
        colors[ImGuiCol_SeparatorHovered] = purple;
        colors[ImGuiCol_SeparatorActive] = purple;
        colors[ImGuiCol_ResizeGrip] = accent;
        colors[ImGuiCol_ResizeGripHovered] = accentLight;
        colors[ImGuiCol_ResizeGripActive] = accentBright;
        colors[ImGuiCol_Tab] = bg2;
        colors[ImGuiCol_TabHovered] = accentLight;
        colors[ImGuiCol_TabActive] = accentLight;
        colors[ImGuiCol_TabUnfocused] = bg1;
        colors[ImGuiCol_TabUnfocusedActive] = bg3;
        colors[ImGuiCol_PlotLines] = accentBright;
        colors[ImGuiCol_PlotLinesHovered] = orange;
        colors[ImGuiCol_PlotHistogram] = cyan;
        colors[ImGuiCol_PlotHistogramHovered] = green;
        colors[ImGuiCol_TableHeaderBg] = bg2;
        colors[ImGuiCol_TableBorderStrong] = border;
        colors[ImGuiCol_TableBorderLight] = bg3;
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.60f, 0.80f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = yellow;
        colors[ImGuiCol_NavHighlight] = accentBright;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    }
    break;
	case 3: // Mocha
    {
        // Corners
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        // Borders
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        // Padding and spacing
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(5.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 21.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 10.0f;

        // Catppuccin Mocha Palette
        const ImVec4 base = ImVec4(0.117f, 0.117f, 0.172f, 1.0f);
        const ImVec4 mantle = ImVec4(0.109f, 0.109f, 0.156f, 1.0f);
        const ImVec4 surface0 = ImVec4(0.200f, 0.207f, 0.286f, 1.0f);
        const ImVec4 surface1 = ImVec4(0.247f, 0.254f, 0.337f, 1.0f);
        const ImVec4 surface2 = ImVec4(0.290f, 0.301f, 0.388f, 1.0f);
        const ImVec4 overlay0 = ImVec4(0.396f, 0.403f, 0.486f, 1.0f);
        const ImVec4 overlay2 = ImVec4(0.576f, 0.584f, 0.654f, 1.0f);
        const ImVec4 text = ImVec4(0.803f, 0.815f, 0.878f, 1.0f);
        const ImVec4 subtext0 = ImVec4(0.639f, 0.658f, 0.764f, 1.0f);
        const ImVec4 mauve = ImVec4(0.796f, 0.698f, 0.972f, 1.0f);
        const ImVec4 peach = ImVec4(0.980f, 0.709f, 0.572f, 1.0f);
        const ImVec4 yellow = ImVec4(0.980f, 0.913f, 0.596f, 1.0f);
        const ImVec4 green = ImVec4(0.650f, 0.890f, 0.631f, 1.0f);
        const ImVec4 teal = ImVec4(0.580f, 0.886f, 0.819f, 1.0f);
        const ImVec4 sapphire = ImVec4(0.458f, 0.784f, 0.878f, 1.0f);
        const ImVec4 blue = ImVec4(0.533f, 0.698f, 0.976f, 1.0f);
        const ImVec4 lavender = ImVec4(0.709f, 0.764f, 0.980f, 1.0f);

        colors[ImGuiCol_Text] = text;
        colors[ImGuiCol_TextDisabled] = subtext0;
        colors[ImGuiCol_WindowBg] = base;
        colors[ImGuiCol_ChildBg] = base;
        colors[ImGuiCol_PopupBg] = surface0;
        colors[ImGuiCol_Border] = surface1;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg] = surface0;
        colors[ImGuiCol_FrameBgHovered] = surface1;
        colors[ImGuiCol_FrameBgActive] = surface2;
        colors[ImGuiCol_TitleBg] = mantle;
        colors[ImGuiCol_TitleBgActive] = surface0;
        colors[ImGuiCol_TitleBgCollapsed] = mantle;
        colors[ImGuiCol_MenuBarBg] = mantle;
        colors[ImGuiCol_ScrollbarBg] = surface0;
        colors[ImGuiCol_ScrollbarGrab] = surface2;
        colors[ImGuiCol_ScrollbarGrabHovered] = overlay0;
        colors[ImGuiCol_ScrollbarGrabActive] = overlay2;
        colors[ImGuiCol_CheckMark] = green;
        colors[ImGuiCol_SliderGrab] = sapphire;
        colors[ImGuiCol_SliderGrabActive] = blue;
        colors[ImGuiCol_Button] = surface0;
        colors[ImGuiCol_ButtonHovered] = surface1;
        colors[ImGuiCol_ButtonActive] = surface2;
        colors[ImGuiCol_Header] = surface0;
        colors[ImGuiCol_HeaderHovered] = surface1;
        colors[ImGuiCol_HeaderActive] = surface2;
        colors[ImGuiCol_Separator] = surface1;
        colors[ImGuiCol_SeparatorHovered] = mauve;
        colors[ImGuiCol_SeparatorActive] = mauve;
        colors[ImGuiCol_ResizeGrip] = surface2;
        colors[ImGuiCol_ResizeGripHovered] = mauve;
        colors[ImGuiCol_ResizeGripActive] = mauve;
        colors[ImGuiCol_Tab] = surface0;
        colors[ImGuiCol_TabHovered] = surface2;
        colors[ImGuiCol_TabActive] = surface1;
        colors[ImGuiCol_TabUnfocused] = surface0;
        colors[ImGuiCol_TabUnfocusedActive] = surface1;
        colors[ImGuiCol_PlotLines] = blue;
        colors[ImGuiCol_PlotLinesHovered] = peach;
        colors[ImGuiCol_PlotHistogram] = teal;
        colors[ImGuiCol_PlotHistogramHovered] = green;
        colors[ImGuiCol_TableHeaderBg] = surface0;
        colors[ImGuiCol_TableBorderStrong] = surface1;
        colors[ImGuiCol_TableBorderLight] = surface0;
        colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
        colors[ImGuiCol_TextSelectedBg] = surface2;
        colors[ImGuiCol_DragDropTarget] = yellow;
        colors[ImGuiCol_NavHighlight] = lavender;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    }
    break;
    case 4: // SimShip
    {
        // Corners
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        // Borders
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        // Padding and spacing
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(5.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 21.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 10.0f;

        // SimShip palette
        const ImVec4 bg0 = ImVec4(0.05f, 0.07f, 0.09f, 1.00f); // darkest background
        const ImVec4 bg1 = ImVec4(0.12f, 0.15f, 0.19f, 1.00f); // bgVeryDark
        const ImVec4 bg2 = ImVec4(0.16f, 0.20f, 0.25f, 1.00f); // bgDark
        const ImVec4 bg3 = ImVec4(0.21f, 0.25f, 0.31f, 1.00f); // bgLight (frame bg)
        const ImVec4 bg4 = ImVec4(0.26f, 0.31f, 0.37f, 1.00f); // frame bg hover discreet
        const ImVec4 overlay0 = ImVec4(0.28f, 0.33f, 0.39f, 1.00f); // overlay discreet
        const ImVec4 overlay1 = ImVec4(0.36f, 0.42f, 0.49f, 1.00f); // overlay medium
        const ImVec4 textLight = ImVec4(0.93f, 0.94f, 0.95f, 1.00f); // main text
        const ImVec4 textDisabled = ImVec4(0.52f, 0.56f, 0.60f, 1.00f); // disabled text
        const ImVec4 accentDark = ImVec4(0.00f, 0.20f, 0.30f, 1.00f); // dark cyan-blue 
        const ImVec4 accent = ImVec4(0.00f, 0.28f, 0.40f, 1.00f); // main cyan-blue
        const ImVec4 accentLight = ImVec4(0.20f, 0.50f, 0.65f, 1.00f); // light cyan-blue
        const ImVec4 accentBright = ImVec4(0.35f, 0.65f, 0.80f, 1.00f); // bright cyan-blue
        const ImVec4 teal = ImVec4(0.20f, 0.70f, 0.75f, 1.00f); // complementary teal
        const ImVec4 yellow = ImVec4(0.85f, 0.78f, 0.30f, 1.00f); // yellow drag&drop
        const ImVec4 orange = ImVec4(0.80f, 0.50f, 0.20f, 1.00f); // orange plot hover
        const ImVec4 purple = ImVec4(0.45f, 0.40f, 0.75f, 1.00f); // separator hover
        const ImVec4 border = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);

        colors[ImGuiCol_Text] = textLight;
        colors[ImGuiCol_TextDisabled] = textDisabled;
        colors[ImGuiCol_WindowBg] = bg1;
        colors[ImGuiCol_ChildBg] = bg2;
        colors[ImGuiCol_PopupBg] = bg2;
        colors[ImGuiCol_Border] = border;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = bg3;
        colors[ImGuiCol_FrameBgHovered] = bg4;
        colors[ImGuiCol_FrameBgActive] = overlay0;
        colors[ImGuiCol_TitleBg] = bg0;
        colors[ImGuiCol_TitleBgActive] = accent;
        colors[ImGuiCol_TitleBgCollapsed] = bg1;
        colors[ImGuiCol_MenuBarBg] = bg2;
        colors[ImGuiCol_ScrollbarBg] = bg0;
        colors[ImGuiCol_ScrollbarGrab] = accent;
        colors[ImGuiCol_ScrollbarGrabHovered] = accentLight;
        colors[ImGuiCol_ScrollbarGrabActive] = accentBright;
        colors[ImGuiCol_CheckMark] = accentBright;
        colors[ImGuiCol_SliderGrab] = accentBright;
        colors[ImGuiCol_SliderGrabActive] = accentBright;
        colors[ImGuiCol_Button] = bg3;
        colors[ImGuiCol_ButtonHovered] = accent;
        colors[ImGuiCol_ButtonActive] = accentLight;
        colors[ImGuiCol_Header] = ImVec4(0.00f, 0.28f, 0.40f, 0.40f);
        colors[ImGuiCol_HeaderHovered] = accent;
        colors[ImGuiCol_HeaderActive] = accentLight;
        colors[ImGuiCol_Separator] = border;
        colors[ImGuiCol_SeparatorHovered] = purple;
        colors[ImGuiCol_SeparatorActive] = purple;
        colors[ImGuiCol_ResizeGrip] = accent;
        colors[ImGuiCol_ResizeGripHovered] = accentLight;
        colors[ImGuiCol_ResizeGripActive] = accentBright;
        colors[ImGuiCol_Tab] = bg2;
        colors[ImGuiCol_TabHovered] = accentLight;
        colors[ImGuiCol_TabActive] = accentLight;
        colors[ImGuiCol_TabUnfocused] = bg1;
        colors[ImGuiCol_TabUnfocusedActive] = bg3;
        colors[ImGuiCol_PlotLines] = accentBright;
        colors[ImGuiCol_PlotLinesHovered] = orange;
        colors[ImGuiCol_PlotHistogram] = teal;
        colors[ImGuiCol_PlotHistogramHovered] = accentBright;
        colors[ImGuiCol_TableHeaderBg] = bg2;
        colors[ImGuiCol_TableBorderStrong] = border;
        colors[ImGuiCol_TableBorderLight] = bg3;
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.28f, 0.40f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = yellow;
        colors[ImGuiCol_NavHighlight] = accentBright;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    }
    break;
    }
}
void InitImGui()
{
    // Create the descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * (uint32_t)(IM_ARRAYSIZE(pool_sizes));
    pool_info.poolSizeCount = (uint32_t)(IM_ARRAYSIZE(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    vkCreateDescriptorPool(g_Device->device, &pool_info, nullptr, &g_ImGuiDescriptorPool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ApplyTheme();

    // Fonts
    io.FontDefault = io.Fonts->AddFontDefault();
    g_FontArial08 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 8.0f);
    g_FontArial10 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 10.0f);
    g_FontArial12 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 12.0f);
    g_FontArial14 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 14.0f);
    g_FontArial16 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 16.0f);
    g_FontArial20 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 20.0f); 
    g_FontArial24 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 24.0f);
    g_FontArial36 = io.Fonts->AddFontFromFileTTF("C://Windows/Fonts/Arial.ttf", 36.0f);
    g_FontCaveat36 = io.Fonts->AddFontFromFileTTF("Resources/Interface/Caveat.ttf", 36.0f);
    g_FontCaveat72 = io.Fonts->AddFontFromFileTTF("Resources/Interface/Caveat.ttf", 72.0f);
    io.Fonts->Build();

    // Backend GLFW
    ImGui_ImplGlfw_InitForVulkan(g_hWindow, true);

    // Backend Vulkan
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_Device->physicalDevice;
    init_info.Device = g_Device->device;
    init_info.QueueFamily = g_Device->familyIndices.graphicsFamily.value();
    init_info.Queue = g_Device->graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = g_ImGuiDescriptorPool;
    init_info.MinImageCount = g_SwapChain->minImageCount;
    init_info.ImageCount = g_SwapChain->imageCount;
    init_info.Allocator = nullptr;
    init_info.PipelineInfoMain.RenderPass = g_RenderPassImGui;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);

    // Icons
    VkDescriptorSetLayout texture_layout = GetImGuiTextureDescriptorSetLayout(g_Device->device);
    
    g_ImgSplashScreen = make_unique<VulkanTexture>();
    g_ImgSplashScreen->CreateFromFile(g_Device, "Resources/Interface/splash_screen.png");
    g_ImgSplashScreen->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgClock = make_unique<VulkanTexture>();
    g_ImgClock->CreateFromFile(g_Device, "Resources/Interface/clock.png");
    g_ImgClock->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);
 
    g_ImgSoundOff = make_unique<VulkanTexture>();
    g_ImgSoundOff->CreateFromFile(g_Device, "Resources/Interface/volume_off.png");
    g_ImgSoundOff->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgSoundUp = make_unique<VulkanTexture>();
    g_ImgSoundUp->CreateFromFile(g_Device, "Resources/Interface/volume_on.png");
    g_ImgSoundUp->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgTimer = make_unique<VulkanTexture>();
    g_ImgTimer->CreateFromFile(g_Device, "Resources/Interface/timer.png");
    g_ImgTimer->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgMto0 = make_unique<VulkanTexture>();
    g_ImgMto0->CreateFromFile(g_Device, "Resources/Interface/shuffle.png");
    g_ImgMto0->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);
    
    g_ImgMto1 = make_unique<VulkanTexture>();
    g_ImgMto1->CreateFromFile(g_Device, "Resources/Interface/clear.png");
    g_ImgMto1->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgMto2 = make_unique<VulkanTexture>();
    g_ImgMto2->CreateFromFile(g_Device, "Resources/Interface/cloud.png");
    g_ImgMto2->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

    g_ImgMto3 = make_unique<VulkanTexture>();
    g_ImgMto3->CreateFromFile(g_Device, "Resources/Interface/foggy.png");
    g_ImgMto3->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

	g_ImgBollard = make_unique<VulkanTexture>();
	g_ImgBollard->CreateFromFile(g_Device, "Resources/Interface/bollard.png");
    g_ImgBollard->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

	g_ImgAnchor = make_unique<VulkanTexture>();
    g_ImgAnchor->CreateFromFile(g_Device, "Resources/Interface/anchor.png");
	g_ImgAnchor->CreateImGuiDescriptor(g_ImGuiDescriptorPool, texture_layout);

#ifdef INFO_INIT    // Structures.h
    wcout << L"-> ImGui Vulkan OK\n";
#endif // INFO_INIT
}
void InitScreenQuad()
{
    g_ScreenQuad = make_unique<ScreenQuad>(g_Device, g_SwapChain);
    g_ScreenQuad->CreateGraphicsPipeline(g_RenderPassPostProcess, VK_SAMPLE_COUNT_1_BIT, g_SwapChain->extent);
    g_ScreenQuad->UpdateDescriptorSet(g_ColorImageResolve->imageView, g_DepthImageResolve->imageView, g_TexBridgeMaskR8->imageView, g_TexBridgeMaskSampler);
}

void RenderLoadingScreen(const char* text)
{
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(g_Device->device, g_SwapChain->swapChain, UINT64_MAX, g_vImageAvailableSemaphores[g_iCurrentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS) return;

    vkWaitForFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame]);

    VkCommandBuffer cmd = g_vCommandBuffers[g_iCurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Fullscreen window with colored background
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f / 255.0f, 53.0f / 255.0f, 75.0f / 255.0f, 1.0f));
    ImGui::Begin("##loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Splash screen in background ===
    ImVec2 screenSize = io.DisplaySize;
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::Image((ImTextureID)g_ImgSplashScreen->GetImGuiDescriptorSet(), ImVec2(screenSize.x, screenSize.y));

    // === Text ===
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();

    ImGui::PushFont(g_FontArial24);
    ImVec2 textSize = ImGui::CalcTextSize(text);
  
    // Color bar
    ImVec2 padding(12.0f, 6.0f);
	ImVec2 barSize(220.0f, 24.0f);
    ImVec2 rectMin( (screenSize.x - barSize.x) * 0.5f - padding.x, (screenSize.y - barSize.y) * 0.5f - padding.y );
    ImVec2 rectMax( (screenSize.x + barSize.x) * 0.5f + padding.x, (screenSize.y + barSize.y) * 0.5f + padding.y );
    drawList->AddRectFilled( ImVec2(windowPos.x + rectMin.x, windowPos.y + rectMin.y), ImVec2(windowPos.x + rectMax.x, windowPos.y + rectMax.y), IM_COL32(1, 53, 75, 255) );

    // Centered text
    ImGui::SetCursorPos(ImVec2((screenSize.x - textSize.x) * 0.5f, (screenSize.y - textSize.y) * 0.5f));
    ImGui::TextUnformatted(text);
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::Render();

    // Render pass with CLEAR (g_RenderPassImGuiLoading)
    VkClearValue clearVal{};
    clearVal.color = { {1.0f / 255.0f, 53.0f / 255.0f, 75.0f / 255.0f, 1.0f} };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = g_RenderPassImGuiLoading;  // loadOp = CLEAR
    rpInfo.framebuffer = g_vFramebuffersImGui[imageIndex];
    rpInfo.renderArea.extent = g_SwapChain->extent;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);

    // Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &g_vImageAvailableSemaphores[g_iCurrentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &g_vRenderFinishedSemaphores[imageIndex];
    vkQueueSubmit(g_Device->graphicsQueue, 1, &submitInfo, g_vImageFences[g_iCurrentFrame]);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_vRenderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_SwapChain->swapChain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(g_Device->presentQueue, &presentInfo);

    vkQueueWaitIdle(g_Device->graphicsQueue);

    g_iCurrentFrame = (g_iCurrentFrame + 1) % g_vImageFences.size();
}
void LoadConfigAndPositions()
{
    const char* filename = "Resources/Config.xml";
    pugi::xml_document doc;

    locale::global(locale("C"));

    if (!doc.load_file(filename))
    {
        g_NoPosition = 0;
        g_NoShip = 0;
        g_TWS_Kt = 0.0f;
        g_TWS_Deg = 0.0f;
        g_vPositions.clear();
        return;
    }

    auto root = doc.child(L"SimShip");

    auto config = root.child(L"Config");
    g_NoPosition = config.child(L"indexPosition").text().as_int();
    g_NoShip = config.child(L"indexShip").text().as_int();
    g_TWS_Kt = config.child(L"windForce").text().as_float();
    g_TWS_Deg = config.child(L"windDir").text().as_float();

    g_vPositions.clear();

    auto positions = root.child(L"Positions");
    for (auto node : positions.children(L"Position"))
    {
        sPositions p;
        p.name = wstring_to_utf8(node.attribute(L"name").as_string());
        p.pos.x = node.attribute(L"lon").as_float();
        p.pos.y = node.attribute(L"lat").as_float();
        p.heading = node.attribute(L"heading").as_float();
        g_vPositions.push_back(p);
    }

    if (g_vPositions.empty())
    {
        sPositions p;
        p.name = "Treac'h er Goured";
        p.pos.x = -2.94097114;
        p.pos.y = 47.3816223;
        p.heading = 90;
        g_vPositions.push_back(p);
        g_NoPosition = 0;
    }
    else if (g_NoPosition >= (int)g_vPositions.size())
    {
        g_NoPosition = (int)g_vPositions.size() - 1;
    }
}
bool RenderSelectionScreen()
{
    uint32_t imageIndex;
    if (vkAcquireNextImageKHR(g_Device->device, g_SwapChain->swapChain, UINT64_MAX, g_vImageAvailableSemaphores[g_iCurrentFrame], VK_NULL_HANDLE, &imageIndex) != VK_SUCCESS) 
        return false;

    vkWaitForFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(g_Device->device, 1, &g_vImageFences[g_iCurrentFrame]);

    VkCommandBuffer cmd = g_vCommandBuffers[g_iCurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;

    // Single fullscreen window — no NoInputs flag so mouse works
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(screen);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.f / 255, 53.f / 255, 75.f / 255, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(29.f / 255, 158.f / 255, 117.f / 255, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(23.f / 255, 134.f / 255, 99.f / 255, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(15.f / 255, 110.f / 255, 86.f / 255, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(200.f / 255, 230.f / 255, 240.f / 255, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));

    ImGui::Begin("##selRoot", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // Background: splash screen image
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::Image((ImTextureID)g_ImgSplashScreen->GetImGuiDescriptorSet(), ImVec2(screen.x, screen.y));

    // Dark overlay on top of splash to improve readability
    dl->AddRectFilled(ImVec2(wp.x, wp.y), ImVec2(wp.x + screen.x, wp.y + screen.y), IM_COL32(1, 35, 50, 220));

    // -------------------------------------------------------
    // Layout constants
    // -------------------------------------------------------
    const float colW = screen.x * 0.28f;
    const float colH = screen.y * 0.68f;
    const float topY = screen.y * 0.13f;
    const float leftX = screen.x * 0.10f;
    const float rightX = screen.x * 0.62f;
    const float itemH = 32.f;
    const float padX = 12.f;

    // Column headers
    auto DrawHeader = [&](const char* label, float x, float y)
        {
            ImGui::PushFont(g_FontArial24);
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(wp.x + x, wp.y + y - ts.y - 10.f), IM_COL32(168, 216, 234, 140), label);
            ImGui::PopFont();
        };

    DrawHeader("SHIP", leftX, topY);
    DrawHeader("START POSITION", rightX, topY);

    // Vertical separator between the two columns
    float sepX = (leftX + colW + rightX) * 0.5f;
    dl->AddLine(ImVec2(wp.x + sepX, wp.y + topY), ImVec2(wp.x + sepX, wp.y + topY + colH), IM_COL32(168, 216, 234, 50), 1.f);

    ImGui::PushFont(g_FontArial20);

    // Replace the two DrawList calls with these:
    for (int i = 0; i < (int)g_vShips.size(); i++)
    {
        ImVec2 rMin(wp.x + leftX, wp.y + topY + i * (itemH + 3.f));
        ImVec2 rMax(wp.x + leftX + colW, rMin.y + itemH);

        bool hovered = ImGui::IsMouseHoveringRect(rMin, rMax);
        if (hovered && ImGui::IsMouseClicked(0)) g_NoShip = i;

        if (i == g_NoShip)
        {
            dl->AddRectFilled(rMin, rMax, IM_COL32(29, 158, 117, 70), 5.f);
            dl->AddRect(rMin, rMax, IM_COL32(29, 158, 117, 200), 5.f, 0, 1.5f);
        }
        else if (hovered)
            dl->AddRectFilled(rMin, rMax, IM_COL32(168, 216, 234, 25), 5.f);

        ImU32 col = (i == g_NoShip) ? IM_COL32(127, 224, 196, 255) : IM_COL32(200, 230, 240, 210);
        float textY = rMin.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(rMin.x + padX, textY), col, g_vShips[i].ShortName.c_str());
    }

    for (int i = 0; i < (int)g_vPositions.size(); i++)
    {
        ImVec2 rMin(wp.x + rightX, wp.y + topY + i * (itemH + 3.f));
        ImVec2 rMax(wp.x + rightX + colW, rMin.y + itemH);

        bool hovered = ImGui::IsMouseHoveringRect(rMin, rMax);
        if (hovered && ImGui::IsMouseClicked(0)) g_NoPosition = i;

        if (i == g_NoPosition)
        {
            dl->AddRectFilled(rMin, rMax, IM_COL32(29, 158, 117, 70), 5.f);
            dl->AddRect(rMin, rMax, IM_COL32(29, 158, 117, 200), 5.f, 0, 1.5f);
        }
        else if (hovered)
            dl->AddRectFilled(rMin, rMax, IM_COL32(168, 216, 234, 25), 5.f);

        ImU32 col = (i == g_NoPosition) ? IM_COL32(127, 224, 196, 255) : IM_COL32(200, 230, 240, 210);
        float textY = rMin.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(rMin.x + padX, textY), col, g_vPositions[i].name.c_str());
    }
    ImGui::PopFont();

    // -------------------------------------------------------
    // "Start" button — centered at the bottom
    // -------------------------------------------------------
    ImGui::PushFont(g_FontArial24);
    const float btnW = 160.f, btnH = 44.f;
    ImGui::SetCursorPos(ImVec2((screen.x - btnW) * 0.5f, topY + colH + 24.f));
    bool startClicked = ImGui::Button("  Start  ", ImVec2(btnW, btnH));
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    ImGui::Render();

    // Vulkan: render pass + submit + present (unchanged from RenderLoadingScreen)
    VkClearValue clearVal{};
    clearVal.color = { {1.f / 255, 53.f / 255, 75.f / 255, 1.f} };

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = g_RenderPassImGuiLoading;
    rpInfo.framebuffer = g_vFramebuffersImGui[imageIndex];
    rpInfo.renderArea.extent = g_SwapChain->extent;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &g_vImageAvailableSemaphores[g_iCurrentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &g_vRenderFinishedSemaphores[imageIndex];
    vkQueueSubmit(g_Device->graphicsQueue, 1, &submitInfo, g_vImageFences[g_iCurrentFrame]);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_vRenderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_SwapChain->swapChain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(g_Device->presentQueue, &presentInfo);

    vkQueueWaitIdle(g_Device->graphicsQueue);
    g_iCurrentFrame = (g_iCurrentFrame + 1) % g_vImageFences.size();

    return startClicked;
}
void SetPosition()
{
    g_Ship->ship.Position = lonlat_to_opengl(g_vPositions[g_NoPosition].pos.x, g_vPositions[g_NoPosition].pos.y);
    g_Ship->SetYawFromHDG(g_vPositions[g_NoPosition].heading);
    g_Ship->ResetVelocities();
    g_Ship->PowerCurrentStep1 = 0;
    g_Ship->PowerCurrentStep2 = 0;
    g_Ship->PropRpm1 = 0.0f;
    g_Ship->PropRpm2 = 0.0f;
    g_Ship->RudderCurrentStep = 0;
    g_Ship->RudderAngleDeg = 0.0f;

    g_Ship->bVisible = true;
    g_Camera.SetFirstUpdate(true);
    if (g_Camera.GetMode() == eCameraMode::BRIDGE)
        g_Camera.KeyboardUpdate(GLFW_KEY_B, 0, 1, 0);
    g_bReset = true;
}
void SetHeading(float heading)
{
    g_Ship->SetYawFromHDG(heading);
    g_Ship->ResetVelocities();
    g_bReset = true;
}
void ReadObjHeader(sTerrain& terrain)
{
    ifstream file(terrain.file);
    string line;
    //static ofstream log("Outputs/terrain_coord.txt", ios::app);

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            istringstream iss(line);
            string token;
            iss >> token;

            if (token == "#")
            {
                iss >> token;
                if (token == "Centre")
                    iss >> terrain.center.x >> terrain.center.y;
                else if (token == "Corner")
                {
                    iss >> token;
                    if (token == "NW")
                        iss >> terrain.xMin >> terrain.zMax;
                    else if (token == "SE")
                        iss >> terrain.xMax >> terrain.zMin;
                }
                else
                {
                    string rest;
                    std::getline(iss, rest);
                    if (!rest.empty() && rest.front() == ' ')
                        rest.erase(rest.begin());
                    terrain.name = Utf8ToAnsi(rest.empty() ? token : token + " " + rest);
                }
            }
        }
        file.close();

        //log << terrain.file << "  (" << terrain.center.x << ", " << terrain.center.y << ")" << "\n";
#ifdef BOUNDS
        if (terrain.xMin != 0.f && terrain.xMax != 0.f && terrain.zMin != 0.f && terrain.zMax != 0.f)
        {
            if (terrain.xMin < XMIN) XMIN = terrain.xMin;
            if (terrain.xMax > XMAX) XMAX = terrain.xMax;
            if (terrain.zMin < ZMIN) ZMIN = terrain.zMin;
            if (terrain.zMax > ZMAX) ZMAX = terrain.zMax;
        }
#endif
        terrain.xMin = lon_to_opengl(terrain.xMin);
        terrain.xMax = lon_to_opengl(terrain.xMax);
        terrain.zMin = lat_to_opengl(terrain.zMin);
        terrain.zMax = lat_to_opengl(terrain.zMax);
    }
}
void LoadTerrains()
{
    if (g_vTerrains.size() == 0)
    {
        vector<string> files = ListFiles("Resources/Terrains/Islands/", ".obj");
        int n = 0;
        for (auto& file : files)
        {
            if (file.find("c001_") != string::npos)
            {
                g_idxHouat = n;
            }
            else
            {
#ifdef DEMO
                continue;
#endif
            }

            if (file.find("c002_") != string::npos)
                g_idxHoedic = n;

            sTerrain terrain;
            terrain.file = file;
            ReadObjHeader(terrain);

            string display = "Loading terrain " + to_string(n + 1) + " ...";
            RenderLoadingScreen(display.c_str());

            terrain.pos = lonlat_to_opengl(terrain.center.x, terrain.center.y);
            terrain.scale = vec3(1.0f);
            terrain.model = make_unique<Model>(g_Device);
            terrain.model->LoadModel(terrain.file.c_str(), VK_FRONT_FACE_CLOCKWISE);
            terrain.model->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
            g_vTerrains.push_back(move(terrain));
            n++;
        }
    }
    if (g_idxHouat >= 0)
    {
        g_Port = make_unique<Model>(g_Device);
        g_Port->LoadModel("Resources/Terrains/Islands/port.gltf");
        g_Port->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
    }
	if (g_idxHoedic >= 0)
	{
		g_Pier = make_unique<Model>(g_Device);
		g_Pier->LoadModel("Resources/Terrains/Islands/pier.gltf");
		g_Pier->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
	}
}
void LoadPortContour()
{
    string filename = "Resources/Terrains/Islands/Contour-port.txt";
    g_vPortLines.clear();

    ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    vector<vec3> vertices;
    string line;

    while (std::getline(file, line))
    {
        stringstream ss(line);
        string prefix;
        ss >> prefix;

        if (prefix == "v")
        {
            // Reading the coordinates of a vertex
            float x, y, z;
            ss >> x >> y >> z;
            vertices.emplace_back(x, y, z);
        }
        else if (prefix == "l")
        {
            // Reading a line by vertex indices (1-based in the file)
            int idx1, idx2;
            ss >> idx1 >> idx2;
            // Validity check of indices (1-based in file -> 0-based in C++)
            if (idx1 > 0 && idx1 <= (int)vertices.size() && idx2 > 0 && idx2 <= (int)vertices.size())
            {
                sLine lineObj;
                lineObj.p1 = vec2(vertices[idx1 - 1].x, vertices[idx1 - 1].z);
                lineObj.p2 = vec2(vertices[idx2 - 1].x, vertices[idx2 - 1].z);
                g_vPortLines.push_back(lineObj);
            }
            else
            {
                cerr << "Invalid vertex index in a row: " << idx1 << ", " << idx2 << endl;
                return;
            }
        }
    }

    mat4 model = glm::translate(mat4(1.0f), g_vTerrains[g_idxHouat].pos);
    for (auto& line : g_vPortLines)
    {
        vec4 p_homo1(line.p1.x, 0.0f, line.p1.y, 1.0f);
        vec4 p_transformed1 = model * p_homo1;
        line.p1 = vec2(p_transformed1.x, p_transformed1.z);
        vec4 p_homo2(line.p2.x, 0.0f, line.p2.y, 1.0f);
        vec4 p_transformed2 = model * p_homo2;
        line.p2 = vec2(p_transformed2.x, p_transformed2.z);
    }
}
void LoadShips()
{
    vector<string> vFiles = ListFiles("Resources/Ships/", ".ini");

    g_vShips.clear();
    for (const auto& file : vFiles)
    {
        Ini ini;
        ini.Load(const_cast<wchar_t*>(utf8_to_wstring(file).c_str()));

        sShip ship;
        wstring ws = L"";

        // Files
        ship.ShortName = wstring_to_utf8(ini.GetString(L"Files", L"ShortName", const_cast<wchar_t*>(ws.c_str())));
#ifdef DEMO
        //if (ship.ShortName != "HMS Clyde")
        //    continue;
#endif
        ship.PathnameHull = wstring_to_utf8(ini.GetString(L"Files", L"PathnameHull", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnameFull = wstring_to_utf8(ini.GetString(L"Files", L"PathnameFull", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnamePropeller1 = wstring_to_utf8(ini.GetString(L"Files", L"PathnamePropeller1", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnamePropeller2 = wstring_to_utf8(ini.GetString(L"Files", L"PathnamePropeller2", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnameRudder = wstring_to_utf8(ini.GetString(L"Files", L"PathnameRudder", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnameRadar1 = wstring_to_utf8(ini.GetString(L"Files", L"PathnameRadar1", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnameRadar2 = wstring_to_utf8(ini.GetString(L"Files", L"PathnameRadar2", const_cast<wchar_t*>(ws.c_str())));
        ship.PathnameFlag = wstring_to_utf8(ini.GetString(L"Files", L"PathnameFlag", const_cast<wchar_t*>(ws.c_str())));
        ship.ThrustSound = wstring_to_utf8(ini.GetString(L"Files", L"ThrustSound", const_cast<wchar_t*>(ws.c_str())));
        ship.BowThrusterSound = wstring_to_utf8(ini.GetString(L"Files", L"BowThrusterSound", const_cast<wchar_t*>(ws.c_str())));
        ship.SternThrusterSound = wstring_to_utf8(ini.GetString(L"Files", L"SternThrusterSound", const_cast<wchar_t*>(ws.c_str())));

        // Positions
        ship.ViewWheel = ini.GetVec3(L"Views", L"ViewWheel", ship.ViewWheel);
        ship.ViewLeft = ini.GetVec3(L"Views", L"ViewLeft", ship.ViewLeft);
        ship.ViewRight = ini.GetVec3(L"Views", L"ViewRight", ship.ViewRight);
        ship.ViewBow = ini.GetVec3(L"Views", L"ViewBow", ship.ViewBow);
        ship.ViewStern = ini.GetVec3(L"Views", L"ViewStern", ship.ViewStern);

        // Dimensions
        ship.Class = static_cast<eClass>(ini.GetInt(L"Dimensions", L"Class", static_cast<int>(ship.Class)));
        ship.Length = ini.GetFloat(L"Dimensions", L"Length", ship.Length);
        ship.SpeedMaxKt = ini.GetFloat(L"Dimensions", L"SpeedMaxKt", ship.SpeedMaxKt);
        ship.SpeedEcoKt = ini.GetFloat(L"Dimensions", L"SpeedEcoKt", ship.SpeedEcoKt);
        ship.Mass_t = ini.GetFloat(L"Dimensions", L"Mass_t", ship.Mass_t);
        ship.PosGravity = ini.GetVec3(L"Dimensions", L"PosGravity", ship.PosGravity);
        ship.PitchRollFactor = ini.GetFloat(L"Dimensions", L"PitchRollFactor", ship.PitchRollFactor);
        ship.EnvMapFactor = ini.GetFloat(L"Dimensions", L"EnvMapFactor", ship.EnvMapFactor);
        ship.ContourType = ini.GetInt(L"Dimensions", L"ContourType", ship.ContourType);
        ship.AreaWetted = ini.GetFloat(L"Dimensions", L"AreaWetted", ship.AreaWetted);
        ship.LWL = ini.GetFloat(L"Dimensions", L"LWL", ship.LWL);
        ship.PositionY = ini.GetFloat(L"Dimensions", L"PositionY", ship.PositionY);
        ship.AreaFront = ini.GetFloat(L"Dimensions", L"AreaFront", ship.AreaFront);
        ship.AreaFrontCenter = ini.GetVec3(L"Dimensions", L"AreaFrontCenter", ship.AreaFrontCenter);
        ship.AreaLat = ini.GetFloat(L"Dimensions", L"AreaLat", ship.AreaLat);
        ship.AreaLatCenter = ini.GetVec3(L"Dimensions", L"AreaLatCenter", ship.AreaLatCenter);

        // Power
        ship.PosPower = ini.GetVec3(L"Power", L"PosPower", ship.PosPower);
        ship.PowerkW = ini.GetFloat(L"Power", L"PowerkW", ship.PowerkW);
        ship.PowerStepMax = ini.GetInt(L"Power", L"PowerStepMax", ship.PowerStepMax);

        // Propellers
        ship.PropRpmMax = ini.GetFloat(L"Propellers", L"PropRpmMax", ship.PropRpmMax);
        ship.PropRpmIncrement = ini.GetFloat(L"Propellers", L"PropRpmIncrement", ship.PropRpmIncrement);
        ship.nPropeller = ini.GetInt(L"Propellers", L"nPropeller", ship.nPropeller);
        ship.PosPropeller1 = ini.GetVec3(L"Propellers", L"PosPropeller1", ship.PosPropeller1);
        ship.PropTorque1 = ini.GetFloat(L"Propellers", L"PropTorque1", ship.PropTorque1);
        ship.PosPropeller2 = ini.GetVec3(L"Propellers", L"PosPropeller2", ship.PosPropeller2);
        ship.PropTorque2 = ini.GetFloat(L"Propellers", L"PropTorque2", ship.PropTorque2);
        ship.PropDiameter = ini.GetFloat(L"Propellers", L"PropDiameter", ship.PropDiameter);
        ship.WakeWidth = ini.GetFloat(L"Propellers", L"WakeWidth", ship.WakeWidth);
        
        // Rudder
        ship.PosRudder = ini.GetVec3(L"Rudder", L"PosRudder", ship.PosRudder);
        ship.RudderIncrement = ini.GetInt(L"Rudder", L"RudderIncrement", ship.RudderIncrement);
        ship.RudderStepMax = ini.GetInt(L"Rudder", L"RudderStepMax", ship.RudderStepMax);
        ship.RudderRotSpeed = ini.GetFloat(L"Rudder", L"RudderRotSpeed", ship.RudderRotSpeed);
        ship.nRudder = ini.GetInt(L"Rudder", L"nRudder", ship.nRudder);
        ship.PosRudder1 = ini.GetVec3(L"Rudder", L"PosRudder1", ship.PosRudder1);
        ship.PosRudder2 = ini.GetVec3(L"Rudder", L"PosRudder2", ship.PosRudder2);

        // Turning
        ship.RoTMax = ini.GetFloat(L"Turning", L"RoTMax", ship.RoTMax);
        ship.TurnabilityAtSpeed = ini.GetFloat(L"Turning", L"TurnabilityAtSpeed", ship.TurnabilityAtSpeed);
        ship.PivotFwd = ini.GetFloat(L"Turning", L"PivotFwd", ship.PivotFwd);
        ship.PivotBwd = ini.GetFloat(L"Turning", L"PivotBwd", ship.PivotBwd);
        ship.CentrifugalPerf = ini.GetFloat(L"Turning", L"CentrifugalPerf", ship.CentrifugalPerf);
        
        // Bow Thruster
        ship.HasBowThruster = ini.GetBoolean(L"BowThruster", L"HasBowThruster", ship.HasBowThruster);
        ship.PosBowThruster = ini.GetVec3(L"BowThruster", L"PosBowThruster", ship.PosBowThruster);
        ship.BowThrusterPerf = ini.GetFloat(L"BowThruster", L"BowThrusterPerf", ship.BowThrusterPerf);
        ship.BowThrusterPowerW = ini.GetFloat(L"BowThruster", L"BowThrusterPowerW", ship.BowThrusterPowerW);
        ship.BowThrusterStepMax = ini.GetInt(L"BowThruster", L"BowThrusterStepMax", ship.BowThrusterStepMax);
        ship.BowThrusterRpmMin = ini.GetFloat(L"BowThruster", L"BowThrusterRpmMin", ship.BowThrusterRpmMin);
        ship.BowThrusterRpmMax = ini.GetFloat(L"BowThruster", L"BowThrusterRpmMax", ship.BowThrusterRpmMax);
        ship.BowThrusterRpmIncrement = ini.GetFloat(L"BowThruster", L"BowThrusterRpmIncrement", ship.BowThrusterRpmIncrement);

        // Stern Thruster
        ship.HasSternThruster = ini.GetBoolean(L"SternThruster", L"HasSternThruster", ship.HasSternThruster);
        ship.PosSternThruster = ini.GetVec3(L"SternThruster", L"PosSternThruster", ship.PosSternThruster);
        ship.SternThrusterPerf = ini.GetFloat(L"SternThruster", L"SternThrusterPerf", ship.SternThrusterPerf);
        ship.SternThrusterPowerW = ini.GetFloat(L"SternThruster", L"SternThrusterPowerW", ship.SternThrusterPowerW);
        ship.SternThrusterStepMax = ini.GetInt(L"SternThruster", L"SternThrusterStepMax", ship.SternThrusterStepMax);
        ship.SternThrusterRpmMin = ini.GetFloat(L"SternThruster", L"SternThrusterRpmMin", ship.SternThrusterRpmMin);
        ship.SternThrusterRpmMax = ini.GetFloat(L"SternThruster", L"SternThrusterRpmMax", ship.SternThrusterRpmMax);
        ship.SternThrusterRpmIncrement = ini.GetFloat(L"SternThruster", L"SternThrusterRpmIncrement", ship.SternThrusterRpmIncrement);

        // Autopilot
        ship.BaseP = ini.GetFloat(L"Autopilot", L"BaseP", ship.BaseP);
        ship.BaseI = ini.GetFloat(L"Autopilot", L"BaseI", ship.BaseI);
        ship.BaseD = ini.GetFloat(L"Autopilot", L"BaseD", ship.BaseD);
        ship.MaxIntegral = ini.GetFloat(L"Autopilot", L"MaxIntegral", ship.MaxIntegral);

        // Radar
        ship.nRadar = ini.GetInt(L"Radar", L"nRadar", ship.nRadar);
        ship.PosRadar1 = ini.GetVec3(L"Radar", L"PosRadar1", ship.PosRadar1);
        ship.RotationRadar1 = ini.GetFloat(L"Radar", L"RotationRadar1", ship.RotationRadar1);
        ship.PosRadar2 = ini.GetVec3(L"Radar", L"PosRadar2", ship.PosRadar2);
        ship.RotationRadar2 = ini.GetFloat(L"Radar", L"RotationRadar2", ship.RotationRadar2);

        // Flag
        ship.bFlag = ini.GetBoolean(L"Flag", L"bFlag", ship.bFlag);
        ship.PosFlag = ini.GetVec3(L"Flag", L"PosFlag", ship.PosFlag);
        ship.DimXFlag = ini.GetFloat(L"Flag", L"DimXFlag", ship.DimXFlag);
        
        // Spray
        ship.SprayVerticalPerf = ini.GetFloat(L"Spray", L"SprayVerticalPerf", ship.SprayVerticalPerf);
        ship.SprayMultiplier = ini.GetInt(L"Spray", L"SprayMultiplier", ship.SprayMultiplier);
        ship.SprayLength = ini.GetFloat(L"Spray", L"SprayLength", ship.SprayLength);
        ship.SprayType = ini.GetInt(L"Spray", L"SprayType", ship.SprayType);

        // Chimneys
        ship.nChimney = ini.GetInt(L"Chimneys", L"nChimney", ship.nChimney);
        ship.PosChimney1 = ini.GetVec3(L"Chimneys", L"PosChimney1", ship.PosChimney1);
        ship.PosChimney2 = ini.GetVec3(L"Chimneys", L"PosChimney2", ship.PosChimney2);

        // Lights
        ship.LightPositions = ini.GetVec3Array(L"Lights", L"LightPositions", ship.LightPositions);
        ship.LightColors = ini.GetVec3Array(L"Lights", L"LightColors", ship.LightColors);

        // Waves
        ship.CenterFore = ini.GetFloat(L"Waves", L"CenterFore", ship.CenterFore);
        ship.BaseFroude = ini.GetInt(L"Waves", L"BaseFroude", ship.BaseFroude);

        g_vShips.push_back(ship);
    }
}
void SetShip(int n)
{
    bool bTakeOldParameters = false;
    vec3 position = vec3(0.0f);
    float surgeVelocity = 0.0f;
    float yawVelocity = 0.0f;
    float yaw = 0.0f;
    float powerCurrentStep1 = 0.0f;
    float powerCurrentStep2 = 0.0f;
    int   nPropeller = 1;
    float propRpm1 = 0.0f;
    float propRpm2 = 0.0f;
    float rudderCurrentStep = 0.0f;
    float rudderAngleDeg = 0.0f;

    if (g_vShips.size() == 1)
        n = 0;

    if (g_vShips.size() > 0 && n >= 0 && n < g_vShips.size())
    {
        if (g_Ship.get() != 0)
        {
            bTakeOldParameters = true;
            position = g_Ship->ship.Position;
            surgeVelocity = g_Ship->SurgeVelocity / g_Ship->ship.SpeedMaxKt;
            yawVelocity = g_Ship->YawVelocity;
            yaw = g_Ship->Yaw;
            powerCurrentStep1 = (float)g_Ship->PowerCurrentStep1 / (float)g_Ship->ship.PowerStepMax;
            powerCurrentStep2 = (float)g_Ship->PowerCurrentStep2 / (float)g_Ship->ship.PowerStepMax;
			nPropeller = g_Ship->ship.nPropeller;
            propRpm1 = g_Ship->PropRpm1 / g_Ship->ship.PropRpmMax;
            propRpm2 = g_Ship->PropRpm2 / g_Ship->ship.PropRpmMax;
            rudderCurrentStep = (float)g_Ship->RudderCurrentStep / (float)g_Ship->ship.RudderStepMax;
            rudderAngleDeg = g_Ship->RudderAngleDeg / (float)g_Ship->ship.RudderStepMax;
        }

        vkDeviceWaitIdle(g_Device->device);
        g_Ship.reset();

        g_NoShip = n;
        g_Ship = make_unique<Ship>(g_Device, 
            g_RenderPassScene, g_SwapChain->extent, 
            g_RenderPassReflection,
            g_RenderPassShadow, g_ShadowWidth, g_ShadowHeight,
            g_RenderPassBridgeMask,
            g_RenderPassWake, g_vShips[g_NoShip], g_Ocean.get(), g_Camera);
        g_Ship->bLights = g_Sky->SunPosition.y < 0.0f ? true : false;
        g_LowMass = g_vShips[g_NoShip].Mass_t / 2;
        g_HighMass = g_vShips[g_NoShip].Mass_t * 2;
        g_Ocean->NeedsUpdateDescriptors = true;
        SetPosition();

        if (bTakeOldParameters)
        {
            g_Ship->ship.Position = position;
            g_Ship->SurgeVelocity = surgeVelocity * g_Ship->ship.SpeedMaxKt;
            g_Ship->YawVelocity = yawVelocity;
            g_Ship->Yaw = yaw;
            g_Ship->PowerCurrentStep1 = powerCurrentStep1 * g_Ship->ship.PowerStepMax;
			if (nPropeller == 2)
                g_Ship->PowerCurrentStep2 = powerCurrentStep2 * g_Ship->ship.PowerStepMax;
            else
                g_Ship->PowerCurrentStep2 = powerCurrentStep1 * g_Ship->ship.PowerStepMax;
            g_Ship->PropRpm1 = propRpm1 * g_Ship->ship.PropRpmMax;
            g_Ship->PropRpm2 = propRpm2 * g_Ship->ship.PropRpmMax;
            g_Ship->RudderCurrentStep = rudderCurrentStep * g_Ship->ship.RudderStepMax;
            g_Ship->RudderAngleDeg = rudderAngleDeg * g_Ship->ship.RudderStepMax;
        }

        g_Ship->HDGInstruction = fmod(450.0f - glm::degrees(g_Ship->Yaw), 360.0f);
        g_bReset = true;
        g_Grid.reset();
        g_Grid = make_unique<GridMesh>(g_Device, g_RenderPassScene, g_SwapChain->extent, 30, g_Ship->ship.Length);
    }
}
void SetMeteo(int n)
{
    auto& rng = [&]() -> std::mt19937& {
        static std::mt19937 gen(std::random_device{}());
        return gen;
        }();

    auto frand = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
        }; 
    
    switch (n)
    {
    case 0: // Random
    {
        g_Clouds->Coverage = frand(0.0f, 1.0f);
        g_Clouds->Crispiness = frand(0.0f, 100.0f);
        g_Clouds->Curliness = frand(0.0f, 3.0f);
        g_Clouds->Absorption = frand(0.0f, 1.5f);
        g_Clouds->Illumination = frand(1.0f, 5.0f);
        g_Clouds->PerlinFrequency = frand(0.0f, 4.0f);
        g_Clouds->ComputeNewWeatherLUT();
        if (frand(0.0f, 1.0f) > 0.5f)
        {
            g_Sky->MistDensity = 0.0f;
            g_Sky->FogDensity = frand(0.0f, 0.01f);
        }
        else
        {
            g_Sky->MistDensity = frand(0.0f, 0.001f);
            g_Sky->FogDensity = 0.0f;
        }
        g_Sky->StoreMistDensity = g_Sky->MistDensity; 
        g_Sky->StoreFogDensity = g_Sky->FogDensity;
        g_TWS_Kt = frand(1.0f, 25.0f);
        g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
        g_Ocean->SetWind(g_Wind);
        g_Clouds->SetCloudSpeed(g_TWS_Kt);
        float rayScaleHeight = frand(0.1f, 20.0f);
        g_Sky->SetRayleighDensity(-1.0 / rayScaleHeight);
        float mieScaleHeight = frand(0.1f, 20.0f);
        g_Sky->SetMieDensity(-1.0 / mieScaleHeight);
    }
    break;
	case 1: // Clear
        g_Clouds->Coverage = 0.0f;
        g_Sky->MistDensity = 0.00001f;
        g_Sky->FogDensity = 0.0f;
        g_Sky->StoreMistDensity = g_Sky->MistDensity;
        g_Sky->StoreFogDensity = g_Sky->FogDensity;
        g_TWS_Kt = 5.0f;
        g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
        g_Ocean->SetWind(g_Wind);
        g_Clouds->SetCloudSpeed(g_TWS_Kt);
        g_Ocean->iOceanColor = 7;
        g_Ocean->OceanColor = g_Ocean->vOceanColors[g_Ocean->iOceanColor];
        break;

    case 2: // Cloudy
        g_Clouds->Coverage = 0.35f;
        g_Clouds->Crispiness = 40.f;
        g_Clouds->Curliness = 0.1f;
        g_Clouds->Density = 0.02f;
        g_Clouds->Absorption = 0.35f;
        g_Clouds->Illumination = 4.0f;
        g_Clouds->PerlinFrequency = 0.8f;
        g_Clouds->ComputeNewWeatherLUT();
        g_Sky->MistDensity = 0.00005f;
        g_Sky->FogDensity = 0.0f;
        g_Sky->StoreMistDensity = g_Sky->MistDensity;
        g_Sky->StoreFogDensity = g_Sky->FogDensity;
        g_TWS_Kt = 15.0f;
        g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
        g_Ocean->SetWind(g_Wind);
        g_Ocean->InitFrequencies();
        g_Clouds->SetCloudSpeed(g_TWS_Kt);
        g_Ocean->iOceanColor = 6;
        g_Ocean->OceanColor = g_Ocean->vOceanColors[g_Ocean->iOceanColor];
        break;

    case 3: // Foggy
        g_Clouds->Coverage = 0.0f;
        g_Sky->MistDensity = 0.0f;
        g_Sky->FogDensity = 0.0025f;
        g_Sky->StoreMistDensity = g_Sky->MistDensity;
        g_Sky->StoreFogDensity = g_Sky->FogDensity;
        g_TWS_Kt = 20.0f;
        g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);
        g_Ocean->SetWind(g_Wind);
        g_Ocean->InitFrequencies();
        g_Clouds->SetCloudSpeed(g_TWS_Kt);
        g_Ocean->iOceanColor = 2;
        g_Ocean->OceanColor = g_Ocean->vOceanColors[g_Ocean->iOceanColor];
        break;
    }
}
void LoadSounds()
{
    g_SoundMgr = SoundManager::getInstance();

    string s = "Resources/Sounds/Seagull_";
    for (int i = 0; i < 8; i++)
    {
        string name = s + to_string(i) + ".wav";
        g_SoundSeagull[i] = make_unique<Sound>(name.c_str());
        g_SoundSeagull[i]->setVolume(1.0f);
        g_SoundSeagull[i]->setPosition(vec3(0.0, 10.0f, 0.0f));
        g_SoundSeagull[i]->adjustDistances();
    }
    g_bSoundSeagull = false;

    g_SoundHorn = make_unique<Sound>("Resources/Sounds/Horn1.wav");
    g_SoundHorn->setVolume(1.0f);
    g_SoundHorn->setPosition(vec3(0.0, 0.0f, 0.0f));
    g_SoundHorn->adjustDistances();

    g_SoundRain = make_unique<Sound>("Resources/Sounds/Rain0.wav");
    g_SoundRain->setVolume(1.0f);
	g_SoundRain->setPitch(1.0f);
    g_SoundRain->setLooping(true);
    g_SoundRain->setPosition(vec3(0.0, 0.0f, 0.0f));
    g_SoundRain->adjustDistances();
}
void UpdateSounds()
{
    if (!g_SoundMgr->bSound)
        return;

    g_SoundMgr->setListenerPosition(g_Camera.GetPosition());
    g_SoundMgr->setListenerOrientation(g_Camera.GetAt(), g_Camera.GetUp());
    g_SoundHorn->setPosition(g_Ship->ship.Position);
    g_SoundRain->setPosition(g_Camera.GetPosition());

    if (g_bSoundSeagull)
    {
        random_device rd;
        mt19937 gen(rd());

        uniform_int_distribution<> distribPlay(0, 1000);
        uniform_int_distribution<> distribSound(0, 6);
        uniform_int_distribution<> distribPosX(-1000, 1000);
        uniform_int_distribution<> distribPosY(-1000, 1000);
        uniform_int_distribution<> distribPosZ(-1000, 1000);
        uniform_real_distribution<> distribVol(0.05f, 1.0f);
        if (distribPlay(gen) < 2)
        {
            int r = distribSound(gen);
            vec3 pos = g_Camera.GetPosition();
            pos.x += distribPosX(gen);
            pos.y += distribPosX(gen);
            pos.z += distribPosZ(gen);
            g_SoundSeagull[r]->setPosition(pos);
            g_SoundSeagull[r]->setVolume(distribVol(gen));
            g_SoundSeagull[r]->play();
        }
    }
}
bool CheckCrossingPort()
{
    float y_plane = 2.0f;   // Height of the port quay
    vec3 bbCorners[8];
    float t;
    vec3 point;
    vector<vec2> vCorners(4);

    sBBox bb = g_Ship->GetBoundingBox();

    bbCorners[0] = g_Ship->TransformPosition(vec3(bb.min.x, bb.min.y, bb.min.z));
    bbCorners[2] = g_Ship->TransformPosition(vec3(bb.min.x, bb.max.y, bb.min.z));
    t = (y_plane - bbCorners[0].y) / (bbCorners[2].y - bbCorners[0].y);
    point = bbCorners[0] + t * (bbCorners[2] - bbCorners[0]);
    vCorners[0] = vec2(point.x, point.z);

    bbCorners[1] = g_Ship->TransformPosition(vec3(bb.max.x, bb.min.y, bb.min.z));
    bbCorners[3] = g_Ship->TransformPosition(vec3(bb.max.x, bb.max.y, bb.min.z));
    t = (y_plane - bbCorners[1].y) / (bbCorners[3].y - bbCorners[1].y);
    point = bbCorners[1] + t * (bbCorners[3] - bbCorners[1]);
    vCorners[1] = vec2(point.x, point.z);

    bbCorners[4] = g_Ship->TransformPosition(vec3(bb.min.x, bb.min.y, bb.max.z));
    bbCorners[6] = g_Ship->TransformPosition(vec3(bb.min.x, bb.max.y, bb.max.z));
    t = (y_plane - bbCorners[4].y) / (bbCorners[6].y - bbCorners[4].y);
    point = bbCorners[4] + t * (bbCorners[6] - bbCorners[4]);
    vCorners[2] = vec2(point.x, point.z);

    bbCorners[5] = g_Ship->TransformPosition(vec3(bb.max.x, bb.min.y, bb.max.z));
    bbCorners[7] = g_Ship->TransformPosition(vec3(bb.max.x, bb.max.y, bb.max.z));
    t = (y_plane - bbCorners[5].y) / (bbCorners[7].y - bbCorners[5].y);
    point = bbCorners[5] + t * (bbCorners[7] - bbCorners[5]);
    vCorners[3] = vec2(point.x, point.z);

    vector<sLine> bbEdges = {
        { vCorners[0], vCorners[1] },
        { vCorners[1], vCorners[2] },
        { vCorners[2], vCorners[3] },
        { vCorners[3], vCorners[0] }
    };

    for (const auto& line : g_vPortLines)
    {
        for (const auto& edge : bbEdges)
        {
            vec3 intersectionPoint;
            if (IntersectionOfSegments(line.p1, line.p2, edge.p1, edge.p2))
            {
                g_CaptureName = L"C R A S H";
                g_CaptureDisplayTime = 1.0f;
                return true; // cut detected
            }
        }
    }
    return false; // no cut detected on the 4 lateral sides
}
void SendNMEA(float time)
{
    if (g_Ship.get() == 0)
        return;

    static double lastTime = 0.0;
    static double lastSentAIVDM1 = 0.0;
    static double lastSentAIVDM5 = 0.0;
    static double initialDelayAIVDM5 = -1.0;

    // Initialize the random number generator only once
    static bool randInitialized = false;
    if (!randInitialized)
    {
        srand(static_cast<unsigned int>(std::time(0)));
        randInitialized = true;
    }

    // Determine the initial random delay for the first AIVDM_5 transmission
    if (initialDelayAIVDM5 < 0.0)
        initialDelayAIVDM5 = (rand() % 60000) / 1000.0; // 0 to 60 seconds

    // Send RMC, VHW, VWR sentences every 50 ms
    if (time - lastTime < 0.05)
        return;

    string sentence = g_Ship->NMEA_RMC();
    if (!sentence.empty())
        g_UdpSender->SendString(sentence);

    sentence = g_Ship->NMEA_VHW();
    if (!sentence.empty())
        g_UdpSender->SendString(sentence);

    sentence = g_Ship->NMEA_VWR();
    if (!sentence.empty())
        g_UdpSender->SendString(sentence);

    // Send NMEA_AIVDM_1 every 2 seconds
    if (time - lastSentAIVDM1 >= 2.0 || lastSentAIVDM1 == 0.0)
    {
        sentence = g_Traffics->NMEA_AIVDM_1();
        if (!sentence.empty())
            g_UdpSender->SendString(sentence);
        lastSentAIVDM1 = time;
    }

    // Send NMEA_AIVDM_5 with initial random delay, then every 60 s, and choose a random index for each transmission
    if ((lastSentAIVDM5 == 0.0 && time >= initialDelayAIVDM5) || (lastSentAIVDM5 > 0.0 && time - lastSentAIVDM5 >= 60.0))
    {
        int index = rand() % g_Traffics->vTraffics.size();
        sentence = g_Traffics->NMEA_AIVDM_5(index);
        if (!sentence.empty())
            g_UdpSender->SendString(sentence);
        lastSentAIVDM5 = time;
    }
    lastTime = time;
}

void InitScene()
{
    // Position, Ship, Wind
    LoadConfigAndPositions();

    // Camera
    g_Camera.SetProjection(45.0f, g_WindowW, g_WindowH, 0.1f, 30000.0f);
    g_Camera.LookAt(vec3(-20.0f, 10.0f, 100.0f), vec3(0.0f, 0.0f, 0.0f));
    g_Camera.SetSpeeds(0.01f, 0.0025f);
    g_Camera.SetMode(eCameraMode::ORBITAL);

    // Time
    RenderLoadingScreen("Initializing sky ..."); 
    g_Sky = make_unique<Sky>(g_Device, g_InitialPosition, g_WindowW, g_WindowH);

    // Wind
    g_Wind = wind_from_speeddir(g_TWS_Deg, g_TWS_Kt);

    // Clouds
    RenderLoadingScreen("Initializing clouds ..."); 
    g_Clouds = make_unique<Clouds>(g_Device, g_SwapChain->extent, g_TWS_Kt);
    g_Clouds->CreatePostPipeline(g_RenderPassScene, g_SwapChain->extent);
    g_Clouds->SetCloudSpeed(g_TWS_Kt);

    // Models
    RenderLoadingScreen("Loading models ..."); 
    g_Axis = make_unique<Model>(g_Device);
    g_Axis->LoadModel("Resources/Interface/Axis.glb");
    g_Axis->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
    g_Axis->bVisible = false;
    
    g_ArrowWind = make_unique<Model>(g_Device);
    g_ArrowWind->LoadModel("Resources/Interface/direction_wind.glb");
    g_ArrowWind->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
    g_ArrowWind->bVisible = false;
    
    for (uint32_t i = 0; i < 5; i++)
    {
        g_BallRed[i] = make_unique<Model>(g_Device);
        g_BallRed[i]->LoadModel("Resources/Buoys/Ball-Red.glb");
        g_BallRed[i]->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
    }
    for (uint32_t i = 0; i < 4; i++)
    {
        g_BallGreen[i] = make_unique<Model>(g_Device);
        g_BallGreen[i]->LoadModel("Resources/Buoys/Ball-Green.glb");
        g_BallGreen[i]->CreateMsPipeline(g_RenderPassScene, g_SwapChain->extent);
    }

    // Ocean
    RenderLoadingScreen("Initializing ocean ...");
    g_Ocean = make_unique<Ocean>(g_Device, g_RenderPassScene, g_SwapChain->extent);
    g_Ocean->Init(g_Wind);
    
    // Terrain
    LoadTerrains();
    LoadPortContour();
    
    // Markup
    g_Markup = make_unique<Markup>(g_Device, g_RenderPassScene, g_SwapChain->extent, L"Resources/Terrains/Islands/Markup-BHH.xml");
    g_Lighthouses = make_unique<Lighthouses>(g_Device, g_RenderPassScene, g_SwapChain->extent, L"Resources/Terrains/Islands/Lighthouses-BHH.xml");

    // Sounds
    LoadSounds();

    // Ships
    RenderLoadingScreen("Loading list of ships ..."); 
    LoadShips();

    g_Traffics = make_unique<Traffics>(g_Device, g_RenderPassScene, g_SwapChain->extent, "Resources/Traffic/traffic.xml");
    
    InitScreenQuad();

    g_UdpSender = make_unique<UdpSender>("127.0.0.1", 54000);
    g_OverlayDisplacement = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);
    g_OverlayGradient = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);
    g_OverlayFoamBuffer = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);

    g_TimerScene.start();

#ifdef BOUNDS
	cout << "Scene bounds: X[" << XMIN << ", " << XMAX << "], Z[" << ZMIN << ", " << ZMAX << "]" << endl;
#endif
}

// Cleanup
void PartialCleanup()
{
    // Wait for the device to be idle
    vkDeviceWaitIdle(g_Device->device);

    // Destroy ImGui
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (g_ImGuiDescriptorPool)
    {
        vkDestroyDescriptorPool(g_Device->device, g_ImGuiDescriptorPool, nullptr);
        g_ImGuiDescriptorPool = VK_NULL_HANDLE;
    }

    // Destroy objects
    g_ScreenQuad.reset();
    g_Sky.reset();
    g_Clouds.reset();
    g_OverlayDisplacement.reset();
    g_OverlayGradient.reset();
    g_OverlayFoamBuffer.reset();

    // Framebuffers
    vkDestroyFramebuffer(g_Device->device, g_FramebufferShadow, nullptr);
    vkDestroyFramebuffer(g_Device->device, g_FramebufferReflection, nullptr);
    vkDestroyFramebuffer(g_Device->device, g_FramebufferWake, nullptr);
    for (auto& fb : g_vFramebuffersSwapChain)
        vkDestroyFramebuffer(g_Device->device, fb, nullptr);
    g_vFramebuffersSwapChain.clear();
    for (auto& fb : g_vFramebuffersPostProcess)
        vkDestroyFramebuffer(g_Device->device, fb, nullptr);
    g_vFramebuffersPostProcess.clear();
    vkDestroyFramebuffer(g_Device->device, g_FramebufferBridgeMask, nullptr);
    for (auto fb : g_vFramebuffersImGui)
        vkDestroyFramebuffer(g_Device->device, fb, nullptr);
    g_vFramebuffersImGui.clear();
    
    // RenderPasses
    vkDestroyRenderPass(g_Device->device, g_RenderPassImGuiLoading, nullptr);
    g_RenderPassImGuiLoading = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassShadow, nullptr);
    g_RenderPassShadow = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassReflection, nullptr);
    g_RenderPassReflection = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassScene, nullptr);
    g_RenderPassScene = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassPostProcess, nullptr);
    g_RenderPassPostProcess = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassImGui, nullptr);
    g_RenderPassImGui = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassWake, nullptr);
    g_RenderPassWake = nullptr;
    vkDestroyRenderPass(g_Device->device, g_RenderPassBridgeMask, nullptr);
    g_RenderPassBridgeMask = nullptr;

    // Images
    g_ColorImage.reset();
    g_DepthImage.reset();
    g_ColorImageResolve.reset();
    g_DepthImageResolve.reset();
    g_TexReflectionColor.reset();
    g_TexReflectionDepth.reset();
    g_TexShadowDepth.reset();
    if (g_TexShadowDepthSampler) 
    {
        vkDestroySampler(g_Device->device, g_TexShadowDepthSampler, nullptr);
        g_TexShadowDepthSampler = VK_NULL_HANDLE;
    }    
    g_TexWake0.reset();
    g_TexWake1.reset();
    g_TexWake2.reset();
    g_TexBridgeMask.reset();
    if (g_TexBridgeMaskSampler) 
    {
        vkDestroySampler(g_Device->device, g_TexBridgeMaskSampler, nullptr);
        g_TexBridgeMaskSampler = VK_NULL_HANDLE;
    }
    g_TexBridgeMaskR8.reset();
    if (g_TexBridgeMaskFramebufferView) 
    {
        vkDestroyImageView(g_Device->device, g_TexBridgeMaskFramebufferView, nullptr);
        g_TexBridgeMaskFramebufferView = VK_NULL_HANDLE;
    }
    if (g_BridgeMaskStagingBuffer) 
    {
        vkDestroyBuffer(g_Device->device, g_BridgeMaskStagingBuffer, nullptr);
        g_BridgeMaskStagingBuffer = VK_NULL_HANDLE;
    }
    if (g_BridgeMaskStagingMemory) 
    {
        vkFreeMemory(g_Device->device, g_BridgeMaskStagingMemory, nullptr);
        g_BridgeMaskStagingMemory = VK_NULL_HANDLE;
    }
    // Swapchain
    for (auto imageView : g_SwapChain->vImageViews)
        vkDestroyImageView(g_Device->device, imageView, nullptr);
    vkDestroySwapchainKHR(g_Device->device, g_SwapChain->swapChain, nullptr);

    // Querypool
    vkDestroyQueryPool(g_Device->device, g_QueryPool, nullptr);

    // Sync objects
    for (size_t i = 0; i < g_vImageAvailableSemaphores.size(); i++)
    {
        vkDestroySemaphore(g_Device->device, g_vImageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(g_Device->device, g_vRenderFinishedSemaphores[i], nullptr);
        vkDestroyFence(g_Device->device, g_vImageFences[i], nullptr);
    }
    g_vImageAvailableSemaphores.clear();
    g_vRenderFinishedSemaphores.clear();
    g_vImageFences.clear();

    if (g_Ocean)
    {
        for (int i = 0; i < 2; i++)
        {
            if (g_Ocean->ComputeFinishedSem[i])
            {
                vkDestroySemaphore(g_Device->device, g_Ocean->ComputeFinishedSem[i], nullptr);
                g_Ocean->ComputeFinishedSem[i] = VK_NULL_HANDLE;
            }
        }
        g_Ocean->ComputeWasPending = false;
    }

    g_SwapChain.reset();
}
void RecreateScene()
{
    // Time
    g_Sky = make_unique<Sky>(g_Device, g_InitialPosition, g_WindowW, g_WindowH);

    // Clouds
    g_Clouds = make_unique<Clouds>(g_Device, g_SwapChain->extent, g_TWS_Kt);
    g_Clouds->CreatePostPipeline(g_RenderPassScene, g_SwapChain->extent);
    g_Clouds->SetCloudSpeed(g_TWS_Kt);

    // Models
    g_Axis->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);
    g_ArrowWind->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);

    for (uint32_t i = 0; i < 5; i++)
        g_BallRed[i]->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);
    for (uint32_t i = 0; i < 4; i++)
        g_BallGreen[i]->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);

	g_Grid->RecreatePipeline(g_RenderPassScene, g_SwapChain->extent);

    // Ocean
    g_Ocean->RecreatePipelines(g_RenderPassScene, g_SwapChain->extent);

    // Terrain
    for (auto& terrain : g_vTerrains)
        terrain.model->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);
    if (g_Port.get())
        g_Port->RecreatePipelines(g_RenderPassScene, nullptr, nullptr, nullptr, g_SwapChain->extent);
   
    // Markup
    g_Markup->RecreatePipelines(g_RenderPassScene, g_SwapChain->extent);
    g_Lighthouses->RecreatePipelines(g_RenderPassScene, g_SwapChain->extent);

    // Ship
    g_Ship->RecreatePipelines(g_RenderPassScene, g_RenderPassReflection, g_RenderPassShadow, g_RenderPassBridgeMask, g_RenderPassWake, g_SwapChain->extent);
    
    g_Traffics->RecreatePipelines(g_RenderPassScene, g_SwapChain->extent);

    InitScreenQuad();

    g_OverlayDisplacement = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);
    g_OverlayGradient = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);
    g_OverlayFoamBuffer = make_unique<Overlay>(g_Device, g_RenderPassScene, g_SwapChain->extent);

    g_TimerScene.start();
}
void PartialRecreate()
{
	/* If resize, only these objects must be recreated:
    Swapchain
    ├── ImageViews
    ├── Images (color, depth, resolve)
    ├── RenderPasses
    ├── Framebuffers
    ├── Pipelines (depend on the extent and the render pass)
    └── Sync objects / QueryPool

    The device, physical device, queues, command pools are never recreated.
    */

    PartialCleanup();

    InitVulkan();
    InitImGui();
    RecreateScene();

    if (g_Ocean)
        g_Ocean->ComputeWasPending = false;

    g_iCurrentFrame = 0;
}
void FinalCleanup()
{
    glfwSetWindowShouldClose(g_hWindow, GLFW_TRUE);
    glfwPollEvents();

    if (g_Device) {
        vkQueueWaitIdle(g_Device->graphicsQueue);
        vkDeviceWaitIdle(g_Device->device);
    }

    // Scene objects (contain VkBuffer, etc.)
    g_Ocean.reset();
    g_Ship.reset();

    g_vTerrains.clear();
    g_vShips.clear();
    g_Port.reset();
    g_Axis.reset();
    g_ArrowWind.reset();
    for (int i = 0; i < 5; i++) g_BallRed[i].reset();
    for (int i = 0; i < 4; i++) g_BallGreen[i].reset();
    g_Markup.reset();
    g_Lighthouses.reset();
    g_Traffics.reset();
    g_UdpSender.reset();

    // Command buffers (free VkCommandBuffer)
    if (g_Device && !g_vCommandBuffers.empty()) {
        vkFreeCommandBuffers(
            g_Device->device,
            g_Device->graphicsCommandPool,
            (uint32_t)g_vCommandBuffers.size(),
            g_vCommandBuffers.data()
        );
        g_vCommandBuffers.clear();
    }

    PartialCleanup();
    g_SwapChain.reset();

    // Surface (before destroying the device)
    if (g_Device && g_Device->surface) {
        vkDestroySurfaceKHR(g_Instance, g_Device->surface, nullptr);
        g_Device->surface = VK_NULL_HANDLE;
    }

    g_Device.reset();

    // Debug messenger (before destroying the instance)
    if (g_DebugMessenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func)
            func(g_Instance, g_DebugMessenger, nullptr);
        g_DebugMessenger = nullptr;
    }

    //vkDestroyInstance(g_Instance, nullptr);
    //glfwDestroyWindow(g_hWindow);
    //glfwTerminate();
    //return EXIT_SUCCESS;
   
    exit(0);
}

int main()
{
#ifdef CONSOLE
    InitConsole();
#endif

    try {
        InitWindow();
        CreateInstance();
        g_Device = make_shared<VulkanDevice>(g_Instance, g_hWindow);
        InitVulkan();
        InitImGui();
        InitScene();

        // --- Selection screen loop (runs before the main loop) ---
        while (!glfwWindowShouldClose(g_hWindow))
        {
            glfwPollEvents();

            if (RenderSelectionScreen())
                break;  // User clicked "Démarrer"
        }

        // Apply the chosen position/ship before entering the main loop
        RenderLoadingScreen("Loading ship ...");
        SetShip(g_NoShip);
        SetPosition();  // à adapter selon votre API

        int prevWidth = 0, prevHeight = 0;
        glfwGetFramebufferSize(g_hWindow, &prevWidth, &prevHeight);

        // FPS limiter
        static auto lastFrameTime = chrono::high_resolution_clock::now();
        static double frameTimeAccumulator = 0.0;

        while (!glfwWindowShouldClose(g_hWindow))
        {
            glfwPollEvents();

            // Camera zoom
            static double fov = g_Camera.GetZoom();
            if (g_bBinoculars && g_Camera.GetPosition().y > 0.0f)
                g_Camera.SetZoom(45.0 / 7.0);
            else
                g_Camera.SetZoom(fov);

            // Time
            double time = g_TimerScene.getTime();

            // Camera update
            if (g_vShips.size() && g_NoShip >= 0 && g_NoShip < g_vShips.size())
            {
                dvec3 orbitalTarget = static_cast<dvec3>(g_Ship->ship.Position) + dvec3(0.0, 10.0, 0.0);
                dvec3 viewPos = dvec3(0.0, 0.0, 0.0);
                switch (g_eBridgeView)
                {
                case eBridgeView::WHEEL:    viewPos = g_Ship->TransformPosition(g_vShips[g_NoShip].ViewWheel); break;
                case eBridgeView::LEFT:     viewPos = g_Ship->TransformPosition(g_vShips[g_NoShip].ViewLeft); break;
                case eBridgeView::RIGHT:    viewPos = g_Ship->TransformPosition(g_vShips[g_NoShip].ViewRight); break;
                case eBridgeView::BOW:      viewPos = g_Ship->TransformPosition(g_vShips[g_NoShip].ViewBow); break;
                case eBridgeView::STERN:    viewPos = g_Ship->TransformPosition(g_vShips[g_NoShip].ViewStern); break;
                }
                // Views -> Look forward
                dvec3 viewTarget = g_Ship->TransformPosition(dvec3(1000.0, viewPos.y, 0.0)) - g_Ship->ship.Position;
                // View -> Look back
                if (g_eBridgeView == eBridgeView::STERN)
                    viewTarget = g_Ship->TransformPosition(dvec3(-1000.0, viewPos.y, 0.0)) - g_Ship->ship.Position;
                g_Camera.Animate(time, orbitalTarget, viewPos, viewTarget);
            }
            else
            {
                dvec3 pos = dvec3(0.0, 0.0, 0.0);
                dvec3 p = dvec3(0.0, 0.0, 0.0);
                dvec3 t = dvec3(0.0, 0.0, 0.0);
                g_Camera.Animate(time, pos, p, t);
            }

            // Update scene
            if (!g_bPause)
            {
                g_Ocean->Update(time, g_iCurrentFrame);
                g_Ship->Update(time);
                g_Ocean->GetRecordFromBuoy(vec2(0.0f, 0.0f), time);
                CheckCrossingPort();
                UpdateSounds();
            }
            else
                g_Ocean->ComputeWasPending = false;  // no compute this frame

            // Pending actions
            if (g_PendingFullscreen)
            {
                g_PendingFullscreen = false;
                SwitchToFullScreen();
            }
            if (g_PendingNoShipChange >= 0)
            {
                SetShip(g_PendingNoShipChange);
                g_PendingNoShipChange = -1;
            }
            
            // FPS limiter
            if (g_bTargetFps)
            {
                auto currentTime = chrono::high_resolution_clock::now();
                auto deltaTime = chrono::duration_cast<chrono::microseconds>(currentTime - lastFrameTime).count();
                frameTimeAccumulator += deltaTime;
                double targetFrameTime = 1000000.0 / g_FpsTarget;

                if (frameTimeAccumulator >= targetFrameTime)
                {
                    Render();   // Render only when ready
                    frameTimeAccumulator -= targetFrameTime;  // Compensation errors
                }

                lastFrameTime = currentTime;
            }
			// No FPS limiter
            else
            {
                Render();
            }
            SendNMEA(time);

        }
    }
    catch (const exception& e) 
    { 
        cerr << e.what() << endl; 
        glfwDestroyWindow(g_hWindow);
        glfwTerminate(); 
        return EXIT_FAILURE; 
    }

    FinalCleanup();
}

#pragma warning(pop)

