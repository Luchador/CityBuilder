#pragma once

struct PanSettings
{
    // The 64-unit source circle is displayed at half size (32 across).
    static constexpr float circleScale = 0.5f;
    float deadZoneRadius = 32.0f * circleScale + 8.0f; // 24-unit radius with margin.
    float speedPerUnit = 4.0f;    // Screen units/second per unit beyond the zone.
    float maximumSpeed = 2400.0f;
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

class Camera
{
public:
    explicit Camera(float extent);

    void Zoom(float steps);
    void ResetZoom();
    float GetHalfHeight() const;

    void SetPosition(float x, float z);
    float GetCenterX() const { return centerX; }
    float GetCenterZ() const { return centerZ; }

    void UpdatePan(float right, float down, float dt, float viewportHeight);

    void BeginMousePan(float x, float y);
    void MoveMousePan(float x, float y);
    void EndMousePan();

    bool IsMousePanning() const { return mousePan.Active(); }

    const AnchoredPan& GetMousePan() const { return mousePan; }
    const PanSettings& GetPanSettings() const { return panSettings; }

private:
    void MoveScreen(float right, float down);

    static constexpr float DefaultHalfHeight = 120.0f;

    float mapExtent;
    float centerX, centerZ;
    float halfHeight = DefaultHalfHeight;

    AnchoredPan mousePan;
    PanSettings panSettings;
};