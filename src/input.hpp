#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <optional>

class Camera;
class ToolController;
class UiManager;

// Application supplies the operations that need its window, world, or renderer.
// All callbacks are required and execute immediately on the main thread.
struct InputActions
{
    std::function<bool()> hasCity;
    std::function<void()> toggleFullscreen;
    std::function<void()> toggleGrid;
    std::function<void(bool)> rotateCamera;
    std::function<void(const SDL_Event&)> handlePanEvent;
    std::function<void()> stopMousePan;
    std::function<bool(float, float)> beginWorldTool;
    std::function<void(float, float)> endWorldTool;
    std::function<void()> cancelWorldTool;
    std::function<void()> clearWorldTool;
};

class Input
{
public:
    // These objects and the callback targets must outlive Input. Refer to the
    // optional controller itself: the creation form can construct it mid-poll.
    Input(UiManager& ui, Camera& camera, std::optional<ToolController>& toolController,
          InputActions actions);
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Drain queued SDL events. False requests that the main loop stop.
    // Call once per frame, before simulation updates and rendering.
    bool PollInput();

private:
    UiManager& ui;
    Camera& camera;
    std::optional<ToolController>& toolController;
    InputActions actions;
};
