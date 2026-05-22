/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"

// 2. LIB
#include <glfw/glfw3.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_angle.hpp>
using namespace glm;

// 3. WIN
#define NOMINMAX
#include <map>
#include <array>
#include <functional>
#include <iostream>
using namespace std; 

using InterpFunc = std::function<double(double)>;

enum eCameraMode{ ORBITAL, BRIDGE, FPS, CAMERA_MODE_COUNT };
enum eBridgeView { WHEEL, LEFT, RIGHT, BOW, STERN };

static double LinInterp(double t) { return t; }
static double SmoothStepInterp(double t) { return t * t * (3.0 - 2.0 * t); }
static double EaseInInterp(double t) { return t * t; }
static double EaseOutInterp(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }
static double EaseInOutInterp(double t) { return t < 0.5 ? 2.0 * t * t : 1.0 - pow(-2.0 * t + 2.0, 2) / 2.0; }

enum eInterpolation { Linear, SmoothStep, EaseIn, EaseOut, EaseInOut, COUNT };


class Camera 
{
public:
	// Init
    void    SetProjection(double fovy, int width, int height, double znear, double zfar);
	void    LookAt(dvec3 posCamera, dvec3 posTarget, dvec3 up = { 0.0, 1.0, 0.0 });
    void    SetZoom(double fovy);
    void    SetSpeeds(double movementSpeed, double rotationSpeed) { mMoveSpeed = movementSpeed; mRotateSpeed = rotationSpeed; }
    void    SetViewportSize(int width, int height);
    void    SetPosition(dvec3 posCamera);
    void    SetInterpolation(eInterpolation type) { mInterpolation = type; }
    void    SetFirstUpdate(bool flag) { bFirstUpdate = flag; }

    // Inputs
    void    KeyboardUpdate(int key, int scancode, int action, int mods);
    void    MousePosUpdate(double xpos, double ypos);
    void    MouseButtonUpdate(int button, int action, int mods);
    void    Animate(double deltaT, dvec3& orbitalTarget, dvec3& viewPos, dvec3& viewTarget);
    InterpFunc mInterpFunc = [](double t) { return t; }; // linear by default

    // Get
    vec3    GetPosition() const { return mPosition; }
    vec3    GetPositionRTE() const { return dvec3(0.0, 0.0, 0.0); }
    mat4    GetProjection()  const { return mMatProjection; }
	mat4    GetView() const { return mMatView; }
	mat4    GetViewProjection() const { return mMatViewProjection; }
    mat4    GetViewRTE() const { return mMatViewRTE; }
    mat4    GetViewProjectionRTE() const { return mMatViewProjectionRTE; }
    mat4    GetViewReflexion() const { return mMatViewReflexion; }
    mat4    GetViewReflexionRTE() const { return mMatViewReflexionRTE; }
    dvec3   GetDirection() { return mDirection; }
    double  GetZoom() const { return mFovyDeg; }
    double  GetNearPlane() const { return mZnear; }
    double  GetFarPlane() const { return mZfar; }
    double  GetInterpolationValue(double t) const;
    eInterpolation GetInterpolation() const { return mInterpolation; }
    bool    IsUnchanged() const { return mIsUnchanged; }

    eCameraMode GetMode() { return mCurrentMode; }
    bool    IsInViewFrustum(const dvec3& position);
    double  GetHorizonViewportY() const;
    double  GetNorthAngleDEG();
    double  GetAttitudeDEG();
    double  GetRollDEG();

    double  GetOrbitRadius() { return mOrbitRadius; }
    int     GetViewportWidth() { return mWindowW; }
    int     GetViewportHeight() { return mWindowH; }

    dvec3   GetAt() const { return mDirection; }
    dvec3   GetUp() const { return mUp; }

    // Orbital mode
    void    SetOrbitalMode();
    void    SetTarget(const dvec3& target);
    void    AdjustOrbitRadius(double delta);

    void    SetMode(eCameraMode mode);
    void    ReturnToPreviousMode();

private:
    void    updateView();
	void    updateViewProjection();
    void    RotateCamera(double yaw, double pitch, double roll);
    void    MoveCamera(const dvec3& delta);

    bool    mIsUnchanged;

    dmat4   mMatView;               // transform from world space to screen UV [0, 1]
	dmat4   mMatProjection;         // projection matrix (view space to clip space)
	dmat4   mMatViewProjection;     // transform from world space to clip space [-1, 1] (WorldToView * Projection)
    dmat4   mMatViewReflexion;      // transform from world space to screen UV [0, 1]

    dmat4   mMatViewRTE;            // transform from world space to screen UV [0, 1]
    dmat4   mMatViewProjectionRTE;  // transform from world space to clip space [-1, 1] (WorldToView * Projection)
    dmat4   mMatViewReflexionRTE;   // transform from world space to screen UV [0, 1]

    double  mMoveSpeed = 0.001;     // movement speed in units/second
    double  mRotateSpeed = 0.0025;  // mouse sensitivity in radians/pixel

	dvec2   mMousePos;
	dvec2   mMousePosPrev;

	dvec3   mPosition;              // in world space
	dvec3   mDirection;             // normalized
	dvec3   mUp;                    // normalized
	dvec3   mRight;                 // normalized

    dvec3   mPositionTarget;
    dvec3   mDirectionTarget;
    dvec3   mUpTarget;
    dvec3   mRightTarget;
    bool    bFirstUpdate = true;
    eInterpolation mInterpolation = eInterpolation::EaseInOut;

    int     mWindowW = 1600;
    int     mWindowH = 1000;
    double  mFovyDeg;
    double  mZnear;
    double  mZfar;
   
    eCameraMode mCurrentMode;
    eCameraMode mPreviousMode;

    // Orbital mode
    dvec3   mTargetPos;
    double  mOrbitRadius;
    double  mOrbitYaw;
    double  mOrbitPitch;

    // Bridge mode — mouse offset in the ship's reference frame
    double  mBridgeYawOffset = 0.0;
    double  mBridgePitchOffset = 0.0;
    double  mPrevShipYaw = 0.0;

    typedef enum
    {
        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        MoveUp,
        MoveDown,

        Orbital,
        Bridge,
        Fps,

        SpeedUp,
        SlowDown,

        KeyboardControlCount,
    } KeyboardControls;

    typedef enum
    {
        Left,
        Middle,
        Right,

        MouseButtonCount,
        MouseButtonFirst = Left,
    } MouseButtons;

    const map<int, int> mKeyboardMap = {
        { GLFW_KEY_W,           KeyboardControls::MoveForward },
        { GLFW_KEY_S,           KeyboardControls::MoveBackward },
        { GLFW_KEY_A,           KeyboardControls::MoveLeft },
        { GLFW_KEY_D,           KeyboardControls::MoveRight },
        { GLFW_KEY_E,           KeyboardControls::MoveUp },
        { GLFW_KEY_Q,           KeyboardControls::MoveDown },
        { GLFW_KEY_C,           KeyboardControls::Orbital },
        { GLFW_KEY_B,           KeyboardControls::Bridge },
        { GLFW_KEY_F,           KeyboardControls::Fps },
        { GLFW_KEY_LEFT_SHIFT,  KeyboardControls::SpeedUp },
        { GLFW_KEY_LEFT_CONTROL,KeyboardControls::SlowDown },
    };

    const std::map<int, int> mMouseButtonMap = {
        { GLFW_MOUSE_BUTTON_LEFT, MouseButtons::Left },
        { GLFW_MOUSE_BUTTON_MIDDLE, MouseButtons::Middle },
        { GLFW_MOUSE_BUTTON_RIGHT, MouseButtons::Right },
    };

    array<bool, KeyboardControls::KeyboardControlCount> mKeyboardState = { false };
    array<bool, MouseButtons::MouseButtonCount> mMouseButtonState = { false };
};
