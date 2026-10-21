#include "ui.hpp"

#include "texture.hpp"
#include "ui_buttons.hpp"
#include "ui_manager.hpp"
#include "ui_minimap.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace
{
    struct ButtonDefinition
    {
        UiButtonId id;
        const char* imageStem; // Relative to assets/, without state suffix or .png.
        const char* tooltip = ""; // Optional; use \n for an explicit line break.
        UiButtonShape shape = UiButtonShape::Rectangle;
        const char* hoverSuffix = "_hov.png";
    };

    constexpr std::array<ButtonDefinition, 5> MinimapButtons{{
        {UiButtonIds::ZoomInButton, "ui/btn_zoom_in", "Zoom In", UiButtonShape::Circle},
        {UiButtonIds::ZoomOutButton, "ui/btn_zoom_out", "Zoom Out", UiButtonShape::Circle},
        {UiButtonIds::RotateClockwiseButton, "ui/btn_rotate_cw", "Rotate Clockwise\nShortcut: ,"},
        {UiButtonIds::RotateCounterClockwiseButton, "ui/btn_rotate_ccw", "Rotate Counterclockwise\nShortcut: ."},
        {UiButtonIds::MinimizeUIButton, "ui/btn_minimize", "Minimize UI\nShortcut: Tab"}
    }};

    constexpr float ToolbarButtonWidth = 84.0f;
    constexpr float ToolbarButtonHeight = 50.0f;
    constexpr float ToolbarButtonGap = 16.0f;
    constexpr float MinimumToolbarButtonGap = 4.0f;

    struct ToolbarButtonDefinition
    {
        ButtonDefinition button;
        float width = ToolbarButtonWidth;
        float height = ToolbarButtonHeight;
    };

    // Display order, top to bottom. Sizes are before the common UI scale.
    constexpr std::array<ToolbarButtonDefinition, 7> ToolbarButtons{{
        {{UiButtonIds::TerrainButton, "ui/toolbar/btn_terrain", "Terrain"}},
        {{UiButtonIds::ZonesButton, "ui/toolbar/btn_zones", "Zones"}},
        {{UiButtonIds::TransportationButton, "ui/toolbar/btn_transp", "Transportation"}},
        {{UiButtonIds::UtilitiesButton, "ui/toolbar/btn_util", "Utilities"}},
        {{UiButtonIds::GovernmentButton, "ui/toolbar/btn_gov", "Government"}},
        {{UiButtonIds::BulldozerButton, "ui/toolbar/btn_bulldozer", "Bulldozer"}, 48.0f, 24.0f},
        {{UiButtonIds::DisastersButton, "ui/toolbar/btn_disaster", "Disasters"}, 48.0f, 24.0f}
    }};

    // Bottom to top, anchored above the minimap rather than below the top group.
    constexpr std::array<ToolbarButtonDefinition, 4> BottomToolbarButtons{{
        {{UiButtonIds::OptionsButton, "ui/toolbar/btn_options", "Options"}},
        {{UiButtonIds::DataButton, "ui/toolbar/btn_data", "Data"}},
        {{UiButtonIds::MeetButton, "ui/toolbar/btn_meet", "Meet with Advisors"}},
        {{UiButtonIds::DayButton, "ui/toolbar/btn_day", "Day / Night"}}
    }};

    constexpr std::array<ButtonDefinition, 6> TickerButtons{{
        {UiButtonIds::TickerPauseButton, "ui/ticker/btn_pause", "Pause Simulation"},
        {UiButtonIds::TickerSpeedButton1, "ui/ticker/btn_tickersquare", "Shuffle"},
        {UiButtonIds::TickerSpeedButton2, "ui/ticker/btn_tickersquare", "Stroll"},
        {UiButtonIds::TickerSpeedButton3, "ui/ticker/btn_tickersquare", "Canter"},
        {UiButtonIds::TickerSpeedButton4, "ui/ticker/btn_tickersquare", "Dash"},
        {UiButtonIds::TickerPlayButton, "ui/ticker/btn_play", "Resume Simulation"}
    }};

    void AddButton(UiButtons& buttons, TextureManager& textures, const ButtonDefinition& definition)
    {
        const std::string stem = definition.imageStem;
    
        auto& button = buttons.Add(definition.id, {
            &textures.Load(stem + "_normal.png"),
            &textures.Load(stem + definition.hoverSuffix),
            &textures.Load(stem + "_pressed.png")
        });
    
        button.SetShape(definition.shape);
        button.SetTooltip(definition.tooltip);
    }

    void LayoutTickerButtons(UiButtons& buttons, const UiLayout& layout)
    {
        const UiRect& area = layout.speedTicker;
        const float controlSize = area.w * 0.5f;

        const auto place = [&](UiButtonId id, UiRect bounds)
        {
            if (auto* button = buttons.Find(id)) 
            {
                button->SetBounds(bounds);
            }
        };

        place(UiButtonIds::TickerPauseButton, {area.x, area.y, controlSize, controlSize});
        place(UiButtonIds::TickerPlayButton, {area.x + controlSize, area.y, controlSize, controlSize});

        constexpr std::array<UiButtonId, 4> speedIds{
            UiButtonIds::TickerSpeedButton1, UiButtonIds::TickerSpeedButton2,
            UiButtonIds::TickerSpeedButton3, UiButtonIds::TickerSpeedButton4
        };

        const float squareSize = area.w / static_cast<float>(speedIds.size());
        const float squareY = area.y + area.h - squareSize;

        for (std::size_t i = 0; i < speedIds.size(); ++i)
        {
            // Adjacent bounds touch exactly; there is no padding between squares.
            place(speedIds[i], {area.x + static_cast<float>(i) * squareSize, squareY, squareSize, squareSize});
        }
    }

    void LayoutToolbarButtons(UiButtons& buttons, const UiLayout& layout)
    {
        const UiRect& area = layout.toolbarContent;
        constexpr float count = static_cast<float>(ToolbarButtons.size() + BottomToolbarButtons.size());
        constexpr float gapCount = count - 1.0f; // Includes separation between groups.
        float stackWidth = 0.0f;
        float buttonHeight = 0.0f;

        const auto measure = [&](const auto& definitions)
        {
            for (const auto& definition : definitions)
            {
                stackWidth = std::max(stackWidth, definition.width);
                buttonHeight += definition.height;
            }
        };

        measure(ToolbarButtons);
        measure(BottomToolbarButtons);

        // Tighten the gaps first, then scale both groups together if still needed.
        // One shared budget keeps the groups apart as window/minimap sizes change.
        const float minimumStackHeight = buttonHeight + gapCount * MinimumToolbarButtonGap;
        const float scale = std::max(0.0f, std::min({layout.scale, area.w / stackWidth, area.h / minimumStackHeight}));
        const float gap = std::max(0.0f, std::min(ToolbarButtonGap * scale, (area.h - buttonHeight * scale) / gapCount));

        const auto place = [&](const ToolbarButtonDefinition& definition, float y)
        {
            const float width = definition.width * scale;
            const float height = definition.height * scale;
            const float x = area.x + (area.w - width) * 0.5f;
            if (auto* button = buttons.Find(definition.button.id))
            {
                button->SetBounds({x, y, width, height});
            }
        };

        float y = area.y;

        for (const auto& definition : ToolbarButtons)
        {
            place(definition, y);
            y += definition.height * scale + gap;
        }

        // Extra room stays between the groups; Options always touches the bottom
        // of the usable toolbar area. Hit bounds follow the same scaled rectangles.
        y = area.y + area.h;

        for (const auto& definition : BottomToolbarButtons)
        {
            y -= definition.height * scale;
            place(definition, y);
            y -= gap;
        }
    }
}

void CreateUiButtons(UiButtons& buttons, TextureManager& textures)
{
    for (const auto& definition : MinimapButtons) 
    {
        AddButton(buttons, textures, definition);
    }

    for (const auto& definition : ToolbarButtons) 
    {
        AddButton(buttons, textures, definition.button);
        buttons.Find(definition.button.id)->SetTooltipDelayMs(0);
    }

    for (const auto& definition : BottomToolbarButtons)
    {
        AddButton(buttons, textures, definition.button);
        buttons.Find(definition.button.id)->SetTooltipDelayMs(0);
    }

    for (const auto& definition : TickerButtons)
    {
        AddButton(buttons, textures, definition);
        // UiManager combines simulation selection with the ticker's group hover.
        buttons.Find(definition.id)->SetInteractionFeedbackEnabled(false);
    }

    CreateTerrainButtons(buttons, textures);
    CreateZoningButtons(buttons, textures);
    CreateTransportationButtons(buttons, textures);
    CreateUtilityButtons(buttons, textures);
    CreateGovernmentButtons(buttons, textures);
}

void LayoutUiButtons(UiButtons& buttons, UiMinimap& minimap, const UiLayout& layout)
{
    LayoutToolbarButtons(buttons, layout);
    LayoutTerrainButtons(buttons, layout);
    LayoutZoningButtons(buttons, layout);
    LayoutTransportationButtons(buttons, layout);
    LayoutUtilityButtons(buttons, layout);
    LayoutGovernmentButtons(buttons, layout);
    LayoutTickerButtons(buttons, layout);

    // Refresh the diamond's bounds before positioning its buttons.
    minimap.SetLayout(layout);

    const UiRect& diamond = minimap.GetDiamondBounds();

    if (diamond.w <= 0.0f || diamond.h <= 0.0f)
    {
        return;
    }

    const float scale = std::min(layout.scale, layout.minimapPanel.w / 224.0f);

    const float size = 32.0f * scale;
    const float gap = 4.0f * scale;
    const float hitInset = size / 64.0f;

    const float halfWidth = diamond.w * 0.5f;
    const float halfHeight = diamond.h * 0.5f;
    const float centerX = diamond.x + halfWidth;
    const float centerY = diamond.y + halfHeight;

    // A direction perpendicular to either lower edge, pointing outward.
    const float edgeLength = std::hypot(halfWidth, halfHeight);
    const float outwardX = halfHeight / edgeLength;
    const float outwardY = halfWidth / edgeLength;

    // Leave room for the full button rectangle, including its corners.
    const float distance = size * 0.5f * (outwardX + outwardY) + gap;

    const float offsetX = outwardX * distance;
    const float offsetY = outwardY * distance;

    // side: -1 = left, +1 = right.
    // alongEdge: 0 = side corner, 1 = bottom corner.
    const auto place = [&](UiButtonId id, float side, float alongEdge)
    {
        if (auto* button = buttons.Find(id))
        {
            const float x = centerX + side * (halfWidth * (1.0f - alongEdge) + offsetX);

            const float y = centerY + halfHeight * alongEdge + offsetY;

            // Convert the button's center into its upper-left corner.
            button->SetBounds({x - size * 0.5f, y - size * 0.5f, size, size});

            button->SetHitInsets({hitInset, hitInset, hitInset, hitInset});
        }
    };

    // Left side: zoom in, followed by clockwise rotation.
    place(UiButtonIds::ZoomInButton, -1.0f, 0.35f);
    place(UiButtonIds::RotateClockwiseButton, -1.0f, 0.65f);

    // Right side: zoom out, followed by counter-clockwise rotation.
    place(UiButtonIds::ZoomOutButton, 1.0f, 0.35f);
    place(UiButtonIds::RotateCounterClockwiseButton, 1.0f, 0.65f);

    // Anchor the minimize button to the minimap panel's bottom-right corner.
    if (auto* button = buttons.Find(UiButtonIds::MinimizeUIButton))
    {
        const UiRect& panel = layout.minimapPanel;
        const float padding = 8.0f * scale;

        button->SetBounds({panel.x + panel.w - padding - size / 1.5f, panel.y + panel.h - padding - size / 1.5f, size / 1.5f, size / 1.5f});

        // The 32x32 images have a one-pixel transparent border.
        button->SetHitInsets({scale, scale, scale, scale});
    }
}

void CancelUiButtonInteraction(UiButtons& buttons)
{
    if (buttons.HasCapture()) 
    {
        SDL_CaptureMouse(false);
    }

    buttons.CancelInteraction();
}

void SetUiMinimized(UiSettings& settings, UiButtons& buttons, bool minimized)
{
    CancelUiButtonInteraction(buttons);
    buttons.ShowOnly(minimized ? std::optional<UiButtonId>{UiButtonIds::MinimizeUIButton} : std::nullopt);
    settings.minimized = minimized;
}

UiButtonResult HandleUiButtonEvent(SDL_Window* window, const SDL_Event& event, const UiSettings& settings, UiButtons& buttons, UiMinimap& minimap)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && buttons.HasOpenFlyout())
    {
        if (!event.key.repeat) CancelUiButtonInteraction(buttons);
        return {true, {}}; // Dismiss the menu before Escape can quit the game.
    }

    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_HIDDEN || event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        CancelUiButtonInteraction(buttons);
        return {}; // Let camera panning also receive these events.
    }

    if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && !buttons.HasCapture())
    {
        buttons.CancelInteraction();
    }

    const bool pointerEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL;

    if (!pointerEvent || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) 
    {
        return {};
    }

    int width = 0, height = 0;

    if (SDL_GetWindowSize(window, &width, &height))
    {
        // Mouse events can arrive before the first frame or immediately after resize.
        LayoutUiButtons(buttons, minimap, CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), settings));
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (event.button.button != SDL_BUTTON_LEFT)
        {
            const bool blocked = buttons.HasCapture() || buttons.BlocksPointer(event.button.x, event.button.y);
            
            if (!blocked && buttons.HasOpenFlyout()) 
            {
                CancelUiButtonInteraction(buttons);
            }
    
            return {blocked, {}};
        }
    
        return buttons.PointerDown(event.button.x, event.button.y);
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
    {
        const bool hadCapture = buttons.HasCapture();
        const auto result = buttons.PointerUp(event.button.x, event.button.y);
    
        if (hadCapture) 
        {
            SDL_CaptureMouse(false);
        }

        return result;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        buttons.PointerMove(event.motion.x, event.motion.y);

        return {buttons.HasCapture() || (buttons.HasOpenFlyout() && buttons.BlocksPointer(event.motion.x, event.motion.y)), {}};
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        return {buttons.HasCapture() || buttons.BlocksPointer(event.wheel.mouse_x, event.wheel.mouse_y), {}};
    }

    return {}; // Right-button releases must still stop camera panning.
}

void UpdateUiButtonPointer(SDL_Window* window, UiButtons& buttons, bool mousePanning)
{
    const auto flags = SDL_GetWindowFlags(window);

    if (!(flags & SDL_WINDOW_INPUT_FOCUS) || mousePanning || (!(flags & SDL_WINDOW_MOUSE_FOCUS) && !buttons.HasCapture()))
    {
        CancelUiButtonInteraction(buttons);
        return;
    }

    float x = 0, y = 0;
    const auto mouseButtons = SDL_GetMouseState(&x, &y);
    const bool hadCapture = buttons.HasCapture();
    buttons.SyncPointer(x, y, (mouseButtons & SDL_BUTTON_LMASK) != 0);

    if (hadCapture && !buttons.HasCapture()) 
    {
        SDL_CaptureMouse(false);
    }
}
