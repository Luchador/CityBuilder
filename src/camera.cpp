#include "camera.hpp"

#include <algorithm>
#include <cmath>

Camera::Camera() = default;

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