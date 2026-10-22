#include "ui_manager.hpp"
#include "ui_zoning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
    constexpr std::array<UiButtonId, 4> TickerSpeedButtons{
        UiButtonIds::TickerSpeedButton1, UiButtonIds::TickerSpeedButton2,
        UiButtonIds::TickerSpeedButton3, UiButtonIds::TickerSpeedButton4
    };
}

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

    tooltip.Hide();
    layout = next;
    LayoutUiButtons(buttons, minimap, layout);
    bottomBarText.SetLayout(layout);

    for (auto& panel : windows) 
    {
        panel->SetScreenSize(layout.width, layout.height);
    }

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
    return !cityCreationPending && GetLayout().IsWorldPoint(x, y) && !buttons.BlocksPointer(x, y) && !WindowAt(x, y);
}

SDL_Rect UiManager::GetWorldViewport(Uint32 pixelWidth, Uint32 pixelHeight)
{
    const auto& current = GetLayout();

    return {0, 0, std::max(1, static_cast<int>(std::lround(current.world.w * static_cast<float>(pixelWidth) / current.width))), std::max(1, static_cast<int>(std::lround(current.world.h * static_cast<float>(pixelHeight) / current.height)))};
}

void UiManager::CancelInteraction()
{
    tooltip.Hide();
    CancelUiButtonInteraction(buttons);
    CancelWindowInteraction();
}

void UiManager::SetMinimized(bool minimized)
{
    if (minimized && cityCreationPending) return;
    if (settings.minimized == minimized) 
    {
        return;
    }

    if (actions.cancelWorldInteraction) 
    {
        actions.cancelWorldInteraction();
    }

    SetUiMinimized(settings, buttons, minimized);
    tooltip.Hide();
    CancelWindowInteraction();
    layoutDirty = true;
    RefreshLayout();
}

void UiManager::ToggleMinimized()
{
    SetMinimized(!settings.minimized);
}

void UiManager::SetSettings(const UiSettings& next)
{
    if (next.minimized && cityCreationPending)
    {
        auto visibleSettings = next;
        visibleSettings.minimized = false;
        SetSettings(visibleSettings);
        return;
    }
    if (settings.barSlimDimension == next.barSlimDimension && settings.minimapSize == next.minimapSize && settings.minimized == next.minimized) 
    {
        return;
    }

    if (actions.cancelWorldInteraction) 
    {
        actions.cancelWorldInteraction();
    }

    settings = next;
    CancelWindowInteraction();
    SetUiMinimized(settings, buttons, next.minimized);
    layoutDirty = true;
    RefreshLayout();
}

bool UiManager::SelectZoneTool(UiButtonId id)
{
    if (!initialized || cityCreationPending) return false;
    if (const auto zone = ZoneTypeForButton(id); zone && actions.selectZoneTool)
    {
        actions.selectZoneTool(*zone);
        SetSelectedToolButton(id);
        // A hotkey can also be used while a toolbar flyout owns mouse capture.
        CancelUiButtonInteraction(buttons);
        tooltip.Hide();
        return true;
    }
    return false;
}

void UiManager::OnUiButtonClicked(UiButtonId id)
{
    if (SelectZoneTool(id)) return;
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
            if (actions.resumeSimulation)
            {
                actions.resumeSimulation();
            }
            break;
        }

        case UiButtonIds::TickerPauseButton:
        {
            if (actions.setSimulationSpeed)
            {
                actions.setSimulationSpeed(0);
            }
            break;
        }

        case UiButtonIds::TickerSpeedButton1:
        {
            if (actions.setSimulationSpeed)
            {
                actions.setSimulationSpeed(1);
            }
            break;
        }

        case UiButtonIds::TickerSpeedButton2:
        {
            if (actions.setSimulationSpeed)
            {
                actions.setSimulationSpeed(2);
            }
            break;
        }

        case UiButtonIds::TickerSpeedButton3:
        {
            if (actions.setSimulationSpeed)
            {
                actions.setSimulationSpeed(3);
            }
            break;
        }

        case UiButtonIds::TickerSpeedButton4:
        {
            if (actions.setSimulationSpeed)
            {
                actions.setSimulationSpeed(4);
            }
            break;
        }
        case UiButtonIds::MeetButton:
        {
            OpenWindow(meetWindowId);
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

void UiManager::SetSelectedToolButton(std::optional<UiButtonId> id)
{
    if (selectedToolButton)
        if (auto* previous = buttons.Find(*selectedToolButton)) previous->SetSelected(false);
    selectedToolButton = id;
    if (id)
        if (auto* next = buttons.Find(*id)) next->SetSelected(true);
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

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL ||
        event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
    {
        tooltip.Hide();
    }

    // Floating windows are drawn above the docks and get first refusal.
    if (HandleWindowEvent(event)) 
    {
        return true;
    }

    if (cityCreationPending)
    {
        // Keep native quit/window events and fullscreen shortcuts available.
        // Everything else belongs to the required creation form, never the docks.
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F11 ||
            (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)))) return false;
        return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
               event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING ||
               event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
               event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL;
    }

    // Dock controls handle their own flyouts and capture.
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

void UiManager::UpdateTicker(int simulationSpeed)
{
    const int speed = std::clamp(simulationSpeed, 0, static_cast<int>(TickerSpeedButtons.size()));
    const bool paused = speed == 0;
    const Uint64 now = SDL_GetTicks();

    if (paused && !tickerPaused)
    {
        // Start each pause with the pressed graphic for a full half second.
        pauseFlashStart = now;
    }
    tickerPaused = paused;

    const bool pauseLit = paused && ((now - pauseFlashStart) / PauseFlashIntervalMs) % 2 == 0;
    const auto select = [this](UiButtonId id, bool selected)
    {
        if (auto* button = buttons.Find(id))
        {
            button->SetSelected(selected);
        }
    };

    select(UiButtonIds::TickerPlayButton, !paused);
    select(UiButtonIds::TickerPauseButton, pauseLit);
    for (std::size_t i = 0; i < TickerSpeedButtons.size(); ++i)
    {
        select(TickerSpeedButtons[i], static_cast<int>(i) < speed);
    }
}

void UiManager::UpdateTickerHover()
{
    std::size_t highlightedSquares = 0;
    for (std::size_t i = 0; i < TickerSpeedButtons.size(); ++i)
    {
        const auto* button = buttons.Find(TickerSpeedButtons[i]);
        if (!settings.minimized && button && button->IsHovered())
        {
            highlightedSquares = i + 1;
        }
    }

    // Hover overlays the current speed display without changing its selection.
    // Recompute the whole prefix so moving left or leaving clears old highlights.
    for (std::size_t i = 0; i < TickerSpeedButtons.size(); ++i)
    {
        if (auto* button = buttons.Find(TickerSpeedButtons[i]))
        {
            button->SetHighlighted(i < highlightedSquares);
        }
    }

    for (UiButtonId id : {UiButtonIds::TickerPlayButton, UiButtonIds::TickerPauseButton})
    {
        if (auto* button = buttons.Find(id))
        {
            button->SetHighlighted(!settings.minimized && button->IsHovered());
        }
    }
}

void UiManager::Update(const City& city, int simulationSpeed)
{
    if (initialized) 
    {
        bottomBarText.UpdateText(city);
        // Real time keeps the pause indicator animating while simulation time stops.
        UpdateTicker(simulationSpeed);
    }
}
