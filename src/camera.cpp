#include "camera.hpp"

#include <algorithm>
#include <cmath>

Camera::Camera(float extent): mapExtent(extent), centerX(extent * 0.5f), centerZ(extent * 0.5f)
{
}

void Camera::Zoom(float steps)
{
    constexpr float ZoomFactor = 0.85f;
    constexpr float MinHalfHeight = 4.0f;
    constexpr float MaxHalfHeight = 200.0f;

    steps = std::clamp(steps, -10.0f, 10.0f);

    halfHeight = std::clamp(halfHeight * std::pow(ZoomFactor, steps), MinHalfHeight, MaxHalfHeight);
}

void Camera::ResetZoom()
{
    halfHeight = DefaultHalfHeight;
}

float Camera::GetHalfHeight() const
{
    return halfHeight;
}

void Camera::SetPosition(float x, float z)
{
    centerX = std::clamp(x, 0.0f, mapExtent);
    centerZ = std::clamp(z, 0.0f, mapExtent);
}

void Camera::MoveScreen(float right, float down)
{
    const GroundPan movement = ScreenToGroundPan(right, down);

    SetPosition(centerX + movement.x, centerZ + movement.z);
}

void Camera::BeginMousePan(float x, float y)
{
    mousePan.Begin(x, y);
}

void Camera::MoveMousePan(float x, float y)
{
    mousePan.Move(x, y);
}

void Camera::EndMousePan()
{
    mousePan.End();
}

void Camera::UpdatePan(float right, float down, float dt, float viewportHeight)
{
    if (mousePan.Active())
    {
        const PanMotion motion = mousePan.Motion(panSettings);

        const float screenToWorld = 2.0f * halfHeight / std::max(1.0f, viewportHeight);

        MoveScreen(motion.x * dt * screenToWorld, motion.y * dt * screenToWorld);

        // Right-button panning takes priority, including its dead zone.
        return;
    }

    const float length = std::hypot(right, down);

    if (length == 0.0f)
    {
        return;
    }

    // Normalize keyboard input so diagonal movement isn't faster.
    const float distance = halfHeight * dt / length;

    MoveScreen(right * distance, down * distance);
}

void AnchoredPan::Begin(float x, float y)
{
    active = true;
    anchorX = cursorX = x;
    anchorY = cursorY = y;
}

void AnchoredPan::Move(float x, float y)
{
    if (!active) 
    {
        return;
    }

    cursorX = x;
    cursorY = y;
}

void AnchoredPan::End()
{
    active = false;
}

PanMotion AnchoredPan::Motion(const PanSettings& settings) const
{
    PanMotion motion;

    if (!active) 
    {
        return motion;
    }

    const float dx = cursorX - anchorX;
    const float dy = cursorY - anchorY;
    const float distance = std::hypot(dx, dy);
    const float radius = std::max(0.0f, settings.deadZoneRadius);

    if (distance <= radius || distance == 0.0f) 
    {
        return motion;
    }

    const float speed = std::min((distance - radius) * std::max(0.0f, settings.speedPerUnit), std::max(0.0f, settings.maximumSpeed));
    motion.x = dx / distance * speed;
    motion.y = dy / distance * speed;
    motion.angle = std::atan2(dy, dx) + 1.57079632679f;
    motion.moving = speed > 0.0f;

    return motion;
}

GroundPan ScreenToGroundPan(float right, float down)
{
    return {0.70710678f * right + 1.29830353f * down, -0.70710678f * right + 1.29830353f * down};
}