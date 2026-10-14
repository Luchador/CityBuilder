#pragma once

struct PanSettings
{
    // Window-coordinate units; circle image is 64 units across.
    float deadZoneRadius = 40.0f; // Circle radius plus an 8-unit margin.
    float speedPerUnit = 4.0f;    // Screen units/second per unit beyond the zone.
    float maximumSpeed = 1200.0f;
};

struct PanMotion
{
    float x = 0, y = 0; // Screen velocity: right is +X, down is +Y.
    float angle = 0;    // Clockwise rotation of an initially upward arrow.
    bool moving = false;
};

class AnchoredPan
{
public:
    void Begin(float x, float y);
    void Move(float x, float y);
    void End();
    PanMotion Motion(const PanSettings& settings) const;

    bool Active() const { return active; }
    float AnchorX() const { return anchorX; }
    float AnchorY() const { return anchorY; }
    float CursorX() const { return cursorX; }
    float CursorY() const { return cursorY; }

private:
    bool active = false;
    float anchorX = 0, anchorY = 0, cursorX = 0, cursorY = 0;
};

struct GroundPan { float x, z; };
// Convert screen displacement (Y down) to the current isometric ground plane.
GroundPan ScreenToGroundPan(float right, float down);
