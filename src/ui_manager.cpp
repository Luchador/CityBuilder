#include "ui_manager.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

void UiManager::RefreshLayout()
{
    if (!initialized) 
    {
        return;
    }

    int width = 0, height = 0;

    if (!SDL_GetWindowSize(window, &width, &height))
    {
        throw std::runtime_error(std::string("Get UI window size: ") + SDL_GetError());
    }

    const auto next = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), settings);

    if (!layoutDirty && next.minimized == layout.minimized && next.width == layout.width &&
        next.height == layout.height && next.minimapPanel.w == layout.minimapPanel.w &&
        next.toolbar.w == layout.toolbar.w && next.bottomBar.h == layout.bottomBar.h)
    {
        return;
    }

    layout = next;
    LayoutUiButtons(buttons, minimap, layout);
    bottomBarText.SetLayout(layout);
    layoutDirty = false;
    geometryDirty = outlineDirty = true;
}

const UiLayout& UiManager::GetLayout()
{
    if (!initialized) 
    {
        throw std::logic_error("Initialize UI before requesting its layout");
    }

    RefreshLayout();

    return layout;
}

bool UiManager::IsWorldPoint(float x, float y)
{
    return GetLayout().IsWorldPoint(x, y) && !buttons.BlocksPointer(x, y);
}

SDL_Rect UiManager::GetWorldViewport(Uint32 pixelWidth, Uint32 pixelHeight)
{
    const auto& current = GetLayout();

    return {0, 0, std::max(1, static_cast<int>(std::lround(current.world.w * static_cast<float>(pixelWidth) / current.width))), std::max(1, static_cast<int>(std::lround(current.world.h * static_cast<float>(pixelHeight) / current.height)))};
}

void UiManager::CancelInteraction()
{
    CancelUiButtonInteraction(buttons);
}

void UiManager::SetMinimized(bool minimized)
{
    if (settings.minimized == minimized) 
    {
        return;
    }

    if (actions.cancelWorldInteraction) 
    {
        actions.cancelWorldInteraction();
    }

    SetUiMinimized(settings, buttons, minimized);
    layoutDirty = true;
    RefreshLayout();
}

void UiManager::ToggleMinimized()
{
    SetMinimized(!settings.minimized);
}

void UiManager::SetSettings(const UiSettings& next)
{
    if (settings.barSlimDimension == next.barSlimDimension && settings.minimapSize == next.minimapSize && settings.minimized == next.minimized) 
    {
        return;
    }

    if (actions.cancelWorldInteraction) 
    {
        actions.cancelWorldInteraction();
    }

    settings = next;
    SetUiMinimized(settings, buttons, next.minimized);
    layoutDirty = true;
    RefreshLayout();
}

void UiManager::OnUiButtonClicked(UiButtonId id)
{
    switch (id)
    {
        case UiButtonIds::ZoomInButton:
        {
            if (actions.zoomCamera) 
            {
                actions.zoomCamera(1.0f);
            }

            break;
        }
        case UiButtonIds::ZoomOutButton:
        {
            if (actions.zoomCamera) 
            {
                actions.zoomCamera(-1.0f);
            }

            break;
        }
        case UiButtonIds::RotateClockwiseButton:
        {
            if (actions.rotateCamera) 
            {
                actions.rotateCamera(true);
            }

            break;
        }
        case UiButtonIds::RotateCounterClockwiseButton:
        {
            if (actions.rotateCamera) 
            {
                actions.rotateCamera(false);
            }

            break;
        }
        case UiButtonIds::MinimizeUIButton:
        {
            ToggleMinimized();
            break;
        }
        case UiButtonIds::TickerPlayButton:
        {
            break;
        }
        case UiButtonIds::TickerPauseButton:
        {
            break;
        }
        case UiButtonIds::TickerSpeedButton1:
        {
            break;
        }
        case UiButtonIds::TickerSpeedButton2:
        {
            break;
        }
        case UiButtonIds::TickerSpeedButton3:
        {
            break;
        }
        case UiButtonIds::TickerSpeedButton4:
        {
            break;
        }
        default:
        {
            if (actions.buttonClicked) 
            {
                actions.buttonClicked(id);
            }
            break;
        }
    }
}

bool UiManager::HandleMinimapEvent(const SDL_Event& event)
{
    if (settings.minimized || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
    {
        return false;
    }

    float mapX = 0.0f, mapZ = 0.0f;

    if (minimap.TryScreenToMap(event.button.x, event.button.y, mapX, mapZ))
    {
        if (actions.cancelWorldInteraction) 
        {
            actions.cancelWorldInteraction();
        }
    
        if (actions.moveCameraToMapPoint) 
        {
            actions.moveCameraToMapPoint(mapX, mapZ);
        }

        return true;
    }
    return false;
}

bool UiManager::HandleEvent(const SDL_Event& event)
{
    if (!initialized) 
    {
        return false;
    }

    RefreshLayout();

    // Existing controls get first refusal, including Escape for open flyouts.
    const auto result = HandleUiButtonEvent(window, event, settings, buttons, minimap);

    if (result.consumed && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        if (actions.cancelWorldInteraction) 
        {
            actions.cancelWorldInteraction();
        }
    }

    if (result.clicked) 
    {
        OnUiButtonClicked(*result.clicked);
    }

    if (result.consumed) 
    {
        return true;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB)
    {
        if (!event.key.repeat) 
        {
            ToggleMinimized();
        }
    
        return true;
    }

    if (HandleMinimapEvent(event)) 
    {
        return true;
    }

    // Reserve the docks as well as the controls. Button releases and window
    // events still reach the world so camera gestures can always terminate.
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !IsWorldPoint(event.button.x, event.button.y))
    {
        if (event.button.button == SDL_BUTTON_LEFT && actions.cancelWorldInteraction)
        {
            actions.cancelWorldInteraction();
        }
    
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        return !IsWorldPoint(event.wheel.mouse_x, event.wheel.mouse_y);
    }

    return false;
}

void UiManager::Update(const City& city)
{
    if (initialized) 
    {
        bottomBarText.UpdateText(city);
    }
}
