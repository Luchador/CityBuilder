#include "pan.hpp"

#include <algorithm>
#include <cmath>

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
