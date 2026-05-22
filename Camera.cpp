/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Camera.h"


bool bInversedY = true;

// Init
void Camera::LookAt(dvec3 cameraPos, dvec3 cameraTarget, dvec3 cameraUp)
{
    this->mPosition = dvec3(-cameraPos.x, cameraPos.y, cameraPos.z);
    this->mDirection = normalize(cameraTarget - cameraPos);
    this->mUp = normalize(cameraUp);
    this->mRight = normalize(cross(this->mUp, this->mDirection));

    updateView();
}
void Camera::SetProjection(double fovy, int width, int height, double znear, double zfar)
{
    mWindowW = width;
    mWindowH = height;
    mFovyDeg = fovy;
    mZnear = znear;
    mZfar = zfar;

    mMatProjection = glm::perspective(glm::radians(fovy), double(width) / double(height), znear, zfar);
    if (bInversedY)
        mMatProjection[1][1] *= -1.0;        // Y flip NDC
    updateViewProjection();
}
void Camera::SetViewportSize(int width, int height)
{
    mWindowW = width;
    mWindowH = height;
    mMatProjection = glm::perspective(glm::radians(mFovyDeg), double(mWindowW) / double(mWindowH), mZnear, mZfar);
    if (bInversedY)
        mMatProjection[1][1] *= -1.0;        // Y flip NDC
    updateViewProjection();
}
void Camera::SetPosition(dvec3 posCamera)
{
    this->mPosition = posCamera;
    updateView();
}
void Camera::SetZoom(double fovy)
{
    mFovyDeg = fovy;
    mMatProjection = glm::perspective(glm::radians(fovy), double(mWindowW) / double(mWindowH), mZnear, mZfar);
    if (bInversedY)
        mMatProjection[1][1] *= -1.0;        // Y flip NDC
    updateViewProjection();
}
// Update
void Camera::RotateCamera(double yaw, double pitch, double roll)
{
    dmat4 rotationMatrix =
        glm::rotate(dmat4(1.0), yaw, dvec3(0.0, 1.0, 0.0)) *
        glm::rotate(dmat4(1.0), pitch, mRight) *
        glm::rotate(dmat4(1.0), roll, mDirection);

    mDirection = dvec3(glm::normalize(rotationMatrix * dvec4(mDirection, 0.0)));
    mUp = dvec3(0.0, 1.0, 0.0);
    mRight = glm::normalize(cross(mUp, mDirection));
}
void Camera::MoveCamera(const dvec3& delta)
{
    mPosition += delta;
}
void Camera::Animate(double deltaT, dvec3& orbitalTarget, dvec3& viewPos, dvec3& viewTarget)
{
    mIsUnchanged = true;
    static eCameraMode previousMode = mCurrentMode;

    double baseT = 0.15f;
    double t = GetInterpolationValue(baseT);

    // Mouse delta management
    dvec2 mouseMove = mMousePos - mMousePosPrev;
    mMousePosPrev = mMousePos;

    // Initializing targets in the first frame
    if (bFirstUpdate)
    {
        mPositionTarget = mPosition;
        mDirectionTarget = mDirection;
        mUpTarget = mUp;
        mRightTarget = mRight;
        t = 1.0;   // No smooth transition, go directly to the new position
        bFirstUpdate = false;
    }

    // Flag to know if the camera has changed mode
    static bool bCameraModeChanged = false;

    // Camera mode change management
    if (mKeyboardState[KeyboardControls::Orbital])
    {
        mCurrentMode = ORBITAL;
        mKeyboardState[KeyboardControls::Orbital] = false;
        bCameraModeChanged = true;
        mIsUnchanged = false;
    }
    else if (mKeyboardState[KeyboardControls::Bridge])
    {
        mCurrentMode = BRIDGE;
        mKeyboardState[KeyboardControls::Bridge] = false;
        bCameraModeChanged = true;
        mIsUnchanged = false;
    }
    else if (mKeyboardState[KeyboardControls::Fps])
    {
        mCurrentMode = FPS;
        mKeyboardState[KeyboardControls::Fps] = false;
        bCameraModeChanged = true;
        mIsUnchanged = false;
    }

    // If we just changed mode we reset the targets to the current state
    if (bCameraModeChanged)
    {
        if (previousMode == FPS && mCurrentMode == ORBITAL)
        {
            // FPS -> ORBITAL :
            mOrbitRadius = glm::length(orbitalTarget - mPosition);
            mOrbitYaw = atan2(mPosition.z - orbitalTarget.z, mPosition.x - orbitalTarget.x);
            mOrbitPitch = -asin((orbitalTarget.y - mPosition.y) / mOrbitRadius);
            mTargetPos = orbitalTarget;
            mDirection = glm::normalize(mTargetPos - mPosition);
            mUp = dvec3(0.0, 1.0, 0.0);
            mRight = glm::normalize(glm::cross(mUp, mDirection));
            mDirectionTarget = mDirection;
            mUpTarget = mUp;
            mRightTarget = mRight;
        }
        else
        {
            mPositionTarget = mPosition;
            if (mCurrentMode == BRIDGE)
            {
                // Compute the ship's yaw at the moment of the switch
                dvec3 shipFwd = normalize(dvec3(viewTarget.x, 0.0, viewTarget.z));
                mPrevShipYaw = atan2(shipFwd.x, shipFwd.z);
                mBridgeYawOffset = 0.0;   // look straight ahead at the switch
                mBridgePitchOffset = 0.0;
                mDirectionTarget = normalize(viewTarget); // initial direction = ship's forward
            }
            else
                mDirectionTarget = mDirection;
            mUpTarget = dvec3(0.0, 1.0, 0.0);
            mRightTarget = mRight;
        }

        bCameraModeChanged = false;
    }
    previousMode = mCurrentMode;

    // ===== ORBITAL MODE =====
    if (mCurrentMode == eCameraMode::ORBITAL)
    {
        if (mMouseButtonState[MouseButtons::Left] && (mouseMove.x || mouseMove.y))
        {
            // There is a new mouse position with a left clic
            mOrbitYaw += mouseMove.x * mRotateSpeed;
            mOrbitPitch += mouseMove.y * mRotateSpeed;
            mOrbitPitch = glm::clamp(mOrbitPitch, -glm::pi<double>() / 2.0 + 0.1, glm::pi<double>() / 2.0 - 0.1);
            mIsUnchanged = false;
        }
        mTargetPos = orbitalTarget;

        double x = mTargetPos.x + mOrbitRadius * cos(mOrbitPitch) * cos(mOrbitYaw);
        double y = mTargetPos.y + mOrbitRadius * sin(mOrbitPitch);
        double z = mTargetPos.z + mOrbitRadius * cos(mOrbitPitch) * sin(mOrbitYaw);
        dvec3 orbitalPosTarget(x, y, z);

        dvec3 directionTarget = normalize(mTargetPos - orbitalPosTarget);
        dvec3 rightTarget = normalize(cross(dvec3(0.0, 1.0, 0.0), directionTarget));
        dvec3 upTarget = dvec3(0.0, 1.0, 0.0);

        mPosition = glm::mix(mPosition, orbitalPosTarget, t);
        mDirection = glm::normalize(glm::mix(mDirection, directionTarget, t));
        mUp = dvec3(0.0, 1.0, 0.0); 
        mRight = glm::normalize(glm::mix(mRight, mRightTarget, t));
    }

    // ===== BRIDGE MODE =====
    else if (mCurrentMode == eCameraMode::BRIDGE)
    {
        // --- 1. Compute the current yaw of the ship from viewTarget ---
        // viewTarget is the forward direction of the ship (normalized) in world space
        vec3 shipForward = normalize(dvec3(viewTarget.x, 0.0, viewTarget.z));
        double shipYaw = atan2(shipForward.x, shipForward.z); // world yaw of the ship

        // --- 2. Accumulation of the mouse offset (in the ship's local frame) ---
        if (mMouseButtonState[MouseButtons::Left] && (mouseMove.x || mouseMove.y))
        {
            mBridgeYawOffset -= mRotateSpeed * mouseMove.x;
            mBridgePitchOffset -= mRotateSpeed * mouseMove.y;
            // Clamp pitch to avoid gimbal lock
            mBridgePitchOffset = glm::clamp(mBridgePitchOffset, -glm::pi<double>() / 2.0 + 0.05, glm::pi<double>() / 2.0 - 0.05);
            mIsUnchanged = false;
        }

        // --- 3. Reconstruct mDirectionTarget from (shipYaw + offset) ---
        // Direction = rotation around Y of the total yaw, then pitch
        double totalYaw = shipYaw + mBridgeYawOffset;

        // Direction in the XZ plane then pitch inclination
        double cosPitch = cos(mBridgePitchOffset);
        dvec3 dir;
        dir.x = sin(totalYaw) * cosPitch;
        dir.y = sin(mBridgePitchOffset);
        dir.z = cos(totalYaw) * cosPitch;
        mDirectionTarget = normalize(dir);

        mUpTarget = dvec3(0.0, 1.0, 0.0);
        mRightTarget = normalize(cross(mUpTarget, mDirectionTarget));

        // --- 4. Interpolation towards the target ---
        mPosition = viewPos;
        mDirection = normalize(mix(mDirection, mDirectionTarget, t));
        mUp = dvec3(0.0, 1.0, 0.0);
        mRight = normalize(mix(mRight, mRightTarget, t));
    }

    // ===== FPS MODE =====
    else if (mCurrentMode == eCameraMode::FPS)
    {
        bool cameraDirty = false;
        double yaw = 0.0, pitch = 0.0, roll = 0.0;
        dvec3 moveVec{ 0.0, 0.0, 0.0 };

        double moveStep = deltaT * mMoveSpeed;
        if (mKeyboardState[KeyboardControls::SpeedUp]) moveStep *= 10.0;
        if (mKeyboardState[KeyboardControls::SlowDown]) moveStep *= 0.1;
        if (mMouseButtonState[MouseButtons::Left] && (mouseMove.x || mouseMove.y))
        {
            yaw = -mRotateSpeed * mouseMove.x;
            pitch = mRotateSpeed * mouseMove.y;
            cameraDirty = true;
        }

        if (mKeyboardState[KeyboardControls::MoveForward])  { moveVec += moveStep * mDirection; cameraDirty = true; }
        if (mKeyboardState[KeyboardControls::MoveBackward]) { moveVec += -moveStep * mDirection; cameraDirty = true; }
        if (mKeyboardState[KeyboardControls::MoveLeft])     { moveVec += moveStep * mRight;     cameraDirty = true; }
        if (mKeyboardState[KeyboardControls::MoveRight])    { moveVec -= moveStep * mRight;     cameraDirty = true; }
        if (mKeyboardState[KeyboardControls::MoveUp])       { moveVec += moveStep * mUp;        cameraDirty = true; }
        if (mKeyboardState[KeyboardControls::MoveDown])     { moveVec -= moveStep * mUp;        cameraDirty = true; }

        if (cameraDirty)
        {
            mPositionTarget += moveVec;

            dmat4 rotationMatrix =
                glm::rotate(dmat4(1.0), yaw, dvec3(0.0, 1.0, 0.0)) *
                glm::rotate(dmat4(1.0), pitch, mRightTarget) *
                glm::rotate(dmat4(1.0), roll, mDirectionTarget);

            mDirectionTarget = glm::normalize(dvec3(rotationMatrix * dvec4(mDirectionTarget, 0.0)));
            mUpTarget = glm::normalize(dvec3(rotationMatrix * dvec4(mUpTarget, 0.0)));
            mRightTarget = glm::normalize(glm::cross(mUpTarget, mDirectionTarget));

            mIsUnchanged = false;
        }

        mPosition = glm::mix(mPosition, mPositionTarget, t);
        mDirection = glm::normalize(glm::mix(mDirection, mDirectionTarget, t));
        mUp = dvec3(0.0, 1.0, 0.0);
        mRight = glm::normalize(glm::mix(mRight, mRightTarget, t));
    }

    updateView();
}

// Get
double Camera::GetInterpolationValue(double t) const
{
    switch (mInterpolation) 
    {
    case eInterpolation::Linear:        return LinInterp(t);
    case eInterpolation::SmoothStep:    return SmoothStepInterp(t);
    case eInterpolation::EaseIn:        return EaseInInterp(t);
    case eInterpolation::EaseOut:       return EaseOutInterp(t);
    case eInterpolation::EaseInOut:     return EaseInOutInterp(t);
    default:                            return t;
    }
}
double Camera::GetNorthAngleDEG()
{
    // Vector representing North (negative Z axis)
    dvec3 north(0.0, 0.0, -1.0);

    // Projection of the direction onto the XZ plane
    dvec3 directionXZ(mDirection.x, 0.0, mDirection.z);

    // Normalization of the projected vector
    directionXZ = normalize(directionXZ);

    // Calculating the angle between the projected direction and North
    double North = glm::orientedAngle(north, directionXZ, dvec3(0.0, 1.0, 0.0));

    // Converting angle to degrees
    North = glm::degrees(North);

    North = 360.0 - North;

    // Adjusting the angle to always be positive (0-360)
    while (North < 0)
        North += 360.0;
    while (North > 360.0)
        North -= 360.0;

    return North;
}
double Camera::GetAttitudeDEG()
{
    // Projection of mDirection onto the horizontal plane (XZ)
    dvec3 horizontalDir = glm::normalize(dvec3(mDirection.x, 0.0, mDirection.z));

    // Calculating the angle between mDirection and its horizontal projection
    double pitchRadians = glm::acos(glm::dot(mDirection, horizontalDir));

    // Determine if the angle is positive or negative
    if (mDirection.y < 0.0) 
        pitchRadians = -pitchRadians;

    // Conversion to degrees
    double pitchDegrees = glm::degrees(pitchRadians);

    return pitchDegrees;
}
double Camera::GetRollDEG()
{
    // Camera "right" vector
    dvec3 cameraRight = glm::normalize(glm::cross(mDirection, dvec3(0.0, 1.0, 0.0)));

    // Camera "up" vector in the plane perpendicular to mDirection
    dvec3 cameraUpProjected = glm::normalize(glm::cross(mDirection, cameraRight));

    // Calculate the angle between cameraUpProjected and the world's "up" vector (0, 1, 0)
    double rollRadians = glm::acos(glm::dot(cameraUpProjected, dvec3(0.0, 1.0, 0.0)));

    // Determine if the angle is positive or negative
    if (glm::dot(cameraRight, mUp) < 0.0)
        rollRadians = -rollRadians;

    // Conversion to degrees
    double rollDegrees = glm::degrees(rollRadians);

    return rollDegrees;
}
bool Camera::IsInViewFrustum(const dvec3& position)
{
    dvec4 clipSpace = mMatViewProjection * dvec4(position, 1.0);
    if (clipSpace.w <= 0.0) return false;
    return std::abs(clipSpace.x) <= clipSpace.w && std::abs(clipSpace.y) <= clipSpace.w && clipSpace.z >= 0.0 && clipSpace.z <= clipSpace.w;
}

double Camera::GetHorizonViewportY() const
{
    // Position far away on the line of sight
    dvec3 distantPoint = mPosition + mZfar * mDirection;

    // Vertical projection on the ground (y=0)
    dvec3 horizonPoint = dvec3(distantPoint.x, 0.0, distantPoint.z);

    // Passage into clip space (viewprojection)
    dvec4 clipPos = mMatViewProjection * dvec4(horizonPoint, 1.0);

    // Homogeneous division
    if (clipPos.w != 0.0)
        clipPos /= clipPos.w;

    // Clamp between -1 and 1 (vertical NDC viewport)
    double viewportY = glm::clamp(clipPos.y, -1.0, 1.0);

    return viewportY;
}

// Inputs
void Camera::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (mKeyboardMap.find(key) == mKeyboardMap.end())
        return;

    auto cameraKey = mKeyboardMap.at(key);
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
        mKeyboardState[cameraKey] = true;
    else 
        mKeyboardState[cameraKey] = false;
}
void Camera::MousePosUpdate(double xpos, double ypos)
{
    mMousePos = { xpos, ypos };
}
void Camera::MouseButtonUpdate(int button, int action, int mods)
{
    if (mMouseButtonMap.find(button) == mMouseButtonMap.end())
        return;

    auto cameraButton = mMouseButtonMap.at(button);
    if (action == GLFW_PRESS)
        mMouseButtonState[cameraButton] = true;
    else 
        mMouseButtonState[cameraButton] = false;
}

// Orbital mode
void Camera::SetOrbitalMode()
{
    mPreviousMode = mCurrentMode;
    mCurrentMode = eCameraMode::ORBITAL;

    dvec3 direction = mTargetPos - mPosition;
    mOrbitRadius = length(direction);
    mOrbitYaw = -atan2(direction.z, direction.x);
    mOrbitPitch = -asin(direction.y / mOrbitRadius);
}
void Camera::SetTarget(const dvec3& target)
{
    mTargetPos = target;
}
void Camera::AdjustOrbitRadius(double delta)
{
    if (mCurrentMode == eCameraMode::ORBITAL)
        mOrbitRadius = glm::max(0.1, mOrbitRadius + delta);
}

void Camera::SetMode(eCameraMode mode)
{
    this->mPreviousMode = mCurrentMode;
    this->mCurrentMode = mode;

    if (mCurrentMode == eCameraMode::ORBITAL)
    {
        dvec3 direction = mTargetPos - mPosition;
        mOrbitRadius = length(direction);
        mOrbitYaw = -atan2(direction.z, direction.x);
        mOrbitPitch = -asin(direction.y / mOrbitRadius);
    }
}
void Camera::ReturnToPreviousMode()
{
    mCurrentMode = mPreviousMode;

    if (mCurrentMode == eCameraMode::ORBITAL)
    {
        dvec3 direction = mTargetPos - mPosition;
        mOrbitRadius = length(direction);
        mOrbitYaw = -atan2(direction.z, direction.x);
        mOrbitPitch = -asin(direction.y / mOrbitRadius);
    }
}

void Camera::updateView()
{
    mMatView = glm::lookAt(mPosition, mPosition + mDirection, mUp);
    mMatViewReflexion = glm::scale(mMatView, dvec3(1.0, -1.0, 1.0));

    mMatViewRTE = mMatView;
    mMatViewRTE[3] = dvec4(0.0, 0.0, 0.0, 1.0);

    updateViewProjection();
}
void Camera::updateViewProjection()
{
    mMatViewProjection = mMatProjection * mMatView;
    mMatViewProjectionRTE = mMatProjection * mMatViewRTE;
}
