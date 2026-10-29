#include "input.hpp"

#include "camera.hpp"
#include "toolcontroller.hpp"
#include "ui/ui_manager.hpp"

#include <stdexcept>
#include <utility>

namespace
{
    std::optional<UiButtonId> ZoneHotkeyButton(SDL_Keycode key)
    {
        switch (key)
        {
            case SDLK_Q: return UiButtonIds::LightResidentialButton;
            case SDLK_W: return UiButtonIds::MediumResidentialButton;
            case SDLK_E: return UiButtonIds::DenseResidentialButton;
            case SDLK_A: return UiButtonIds::LightCommercialButton;
            case SDLK_S: return UiButtonIds::MediumCommercialButton;
            case SDLK_D: return UiButtonIds::DenseCommercialButton;
            case SDLK_Z: return UiButtonIds::FarmButton;
            case SDLK_X: return UiButtonIds::MediumIndustrialButton;
            case SDLK_C: return UiButtonIds::HeavyIndustrialButton;
            default: return std::nullopt;
        }
    }
}

Input::Input(UiManager& uiManager, Camera& gameCamera, std::optional<ToolController>& tools, InputActions callbacks) : ui(uiManager), camera(gameCamera), toolController(tools), actions(std::move(callbacks))
{
    if (!actions.hasCity || !actions.toggleFullscreen || !actions.toggleGrid ||
        !actions.rotateCamera || !actions.handlePanEvent || !actions.stopMousePan ||
        !actions.beginWorldTool || !actions.endWorldTool ||
        !actions.cancelWorldTool || !actions.clearWorldTool)
    {
        throw std::invalid_argument("Input requires all application callbacks");
    }
}

bool Input::PollInput()
{
    bool running = true;
    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        // Global UI shortcut: process before a focused text box handles Ctrl+C.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_C && (event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT) && !(event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)))
        {
            if (!event.key.repeat) 
            {
                ui.OpenDebugConsole();
            }
            continue;
        }

        // Termination must not depend on the UI declining this event.
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_HIDDEN || event.type == SDL_EVENT_WINDOW_RESIZED 
            || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB))
        {
            actions.cancelWorldTool();
        }

        const bool wasDragging = toolController && toolController->IsInteracting();
        const bool consumed = ui.HandleEvent(event);

        if (wasDragging && event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        {
            if (consumed) 
            {
                actions.cancelWorldTool();
            }
            else 
            {
                actions.endWorldTool(event.button.x, event.button.y);
            }
            
            continue;
        }

        if (consumed)
        {
            continue;
        }

        if (event.type == SDL_EVENT_QUIT)
        {
            running = false;
            continue;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key == SDLK_F11 || (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT))))
        {
            actions.toggleFullscreen();
            continue;
        }

        // Query after UI handling: Accept may have created a city just now.
        if (!actions.hasCity())
        {
            continue;
        }

        actions.handlePanEvent(event);

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && toolController)
        {
            if (actions.beginWorldTool(event.button.x, event.button.y))
            {
                continue;
            }
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            // UI text entry has already had first refusal. These are bare-key
            // shortcuts; modified combinations remain available to other commands.
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)))
            {
                if (const auto id = ZoneHotkeyButton(event.key.key); id && ui.SelectZoneTool(*id))
                {
                    continue;
                }

                if (event.key.key == SDLK_R && ui.SelectRoadTool())
                {
                    continue;
                }
            }

            if (event.key.key == SDLK_ESCAPE)
            {
                if (toolController && toolController->HasTool())
                {
                    if (toolController->IsInteracting())
                    {
                        actions.cancelWorldTool();
                    }
                    else
                    {
                        actions.clearWorldTool();
                    }

                    continue;
                }
                running = false;
            }

            if (event.key.key == SDLK_G)
            {
                actions.toggleGrid();
            }

            if (event.key.key == SDLK_COMMA)
            {
                actions.rotateCamera(true);
            }

            if (event.key.key == SDLK_PERIOD)
            {
                actions.rotateCamera(false);
            }

            if (event.key.key == SDLK_HOME)
            {
                actions.cancelWorldTool();
                actions.stopMousePan();
                camera.SetPosition(camera.GetMapExtent() * 0.5f, camera.GetMapExtent() * 0.5f);
                camera.ResetZoom();
            }
        }

        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            actions.cancelWorldTool();
            float amount = event.wheel.y;

            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            {
                amount = -amount;
            }

            camera.Zoom(amount);
        }
    }

    return running;
}
