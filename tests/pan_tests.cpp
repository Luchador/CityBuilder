#include "camera.hpp"
#include "ui.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static bool Near(float a, float b, float tolerance = 0.001f)
{
    return std::abs(a - b) <= tolerance;
}

int main()
{
    AnchoredPan pan;
    PanSettings settings;
    Check(!pan.Active() && !pan.Motion(settings).moving, "inactive by default");
    pan.Begin(300, 200);
    Check(pan.Active() && !pan.Motion(settings).moving, "press starts neutral");
    for (float offset : {0.f, 20.f, 32.f, 39.9f, 40.f})
    {
        pan.Move(300 + offset, 200);
        Check(!pan.Motion(settings).moving, "dead zone includes circle and margin");
    }
    pan.Move(341, 200);
    Check(Near(pan.Motion(settings).x, 4), "continuous start after dead zone");
    pan.Move(400, 200);
    Check(Near(pan.Motion(settings).x, 240), "distance controls speed");
    pan.Move(460, 200);
    Check(Near(pan.Motion(settings).x, 480), "speed rises with distance");
    pan.Move(2300, 200);
    Check(Near(pan.Motion(settings).x, 1200), "speed is capped");
    Check(Near(pan.AnchorX(), 300) && Near(pan.AnchorY(), 200), "anchor stays fixed");

    // Cardinals and diagonals: arrow and projected camera travel agree.
    for (int i = 0; i < 8; ++i)
    {
        const float angle = static_cast<float>(i) * 0.78539816339f;
        const float dx = std::cos(angle), dy = std::sin(angle);
        pan.Move(300 + dx * 100, 200 + dy * 100);
        const auto motion = pan.Motion(settings);
        Check(Near(std::hypot(motion.x, motion.y), 240), "speed independent of direction");
        Check(Near(std::sin(motion.angle), dx) && Near(-std::cos(motion.angle), dy),
              "upward source arrow rotates toward movement");
        const auto ground = ScreenToGroundPan(motion.x, motion.y);
        const float screenX = (ground.x - ground.z) * 0.70710678f;
        const float screenY = (ground.x + ground.z) * 0.40824829f;
        Check(Near(screenX, motion.x) && Near(screenY, motion.y), "camera projection matches cursor");
    }

    pan.Move(300, 200);
    Check(!pan.Motion(settings).moving, "return to anchor stops");
    pan.End();
    pan.Move(999, 999);
    Check(!pan.Active() && !pan.Motion(settings).moving, "release cancels movement");
    pan.Begin(80, 90);
    Check(Near(pan.AnchorX(), 80) && Near(pan.CursorY(), 90), "new press resets anchor and cursor");
    pan.Move(-80, -90);
    Check(pan.Motion(settings).moving, "captured coordinates may be outside window");
    pan.End();

    // One-second movement remains equal across frame rates and zoom levels
    // after projection back to screen coordinates.
    for (int fps : {30, 60, 144})
    for (float zoom : {4.f, 40.f, 120.f, 200.f})
    {
        GroundPan sum{};
        for (int frame = 0; frame < fps; ++frame)
        {
            const float step = 240.f / static_cast<float>(fps) * (2 * zoom / 628.f);
            const auto ground = ScreenToGroundPan(step, 0);
            sum.x += ground.x; sum.z += ground.z;
        }
        const float pixels = (sum.x - sum.z) * 0.70710678f * 628.f / (2 * zoom);
        Check(Near(pixels, 240.f, 0.002f), "frame-rate and zoom independent visual speed");
    }

    for (float mapSize : {128.f, 224.f, 400.f})
    {
        UiSettings ui;
        ui.minimapSize = mapSize;
        const auto layout = CalculateUiLayout(1280, 720, ui);
        Check(layout.IsWorldPoint(100, 100), "viewport allows start");
        for (UiRect rect : {layout.toolbarContent, layout.newsTicker, layout.minimapContent})
            Check(!layout.IsWorldPoint(rect.x + rect.w / 2, rect.y + rect.h / 2), "UI blocks gesture start");
    }
    std::cout << "PASS: pan dead zone, speed curve/cap, anchor/reset, eight directions, projection, frame rates, zoom, and UI hit tests\n";
}
