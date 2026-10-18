#include "ui.hpp"

#include "texture.hpp"
#include "ui_buttons.hpp"
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
        UiButtonShape shape = UiButtonShape::Rectangle;
        const char* hoverSuffix = "_hov.png";
    };

    constexpr std::array<ButtonDefinition, 5> MinimapButtons{{
        {UiButtonIds::ZoomInButton, "ui/btn_zoom_in", UiButtonShape::Circle},
        {UiButtonIds::ZoomOutButton, "ui/btn_zoom_out", UiButtonShape::Circle},
        {UiButtonIds::RotateClockwiseButton, "ui/btn_rotate_cw"},
        {UiButtonIds::RotateCounterClockwiseButton, "ui/btn_rotate_ccw"},
        {UiButtonIds::MinimizeUIButton, "ui/btn_minimize"}
    }};

    // Display order, top to bottom. These buttons have no game actions yet.
    constexpr std::array<ButtonDefinition, 6> ToolbarButtons{{
        {UiButtonIds::TerrainButton, "ui/toolbar/btn_terrain"},
        {UiButtonIds::ZonesButton, "ui/toolbar/btn_zones"},
        {UiButtonIds::InfrastructureButton, "ui/toolbar/btn_infra"},
        {UiButtonIds::UtilitiesButton, "ui/toolbar/btn_util"},
        {UiButtonIds::GovernmentButton, "ui/toolbar/btn_gov"},
        {UiButtonIds::DisastersButton, "ui/toolbar/btn_disaster"}
    }};

    constexpr float ToolbarButtonWidth = 84.0f;
    constexpr float ToolbarButtonHeight = 50.0f;
    constexpr float ToolbarButtonGap = 8.0f;

    // Nearest the toolbar first, extending to the left. These are placeholders;
    // IDs are returned on release, ready for tool actions or more menus later.
    constexpr std::array<ButtonDefinition, 4> ZoningButtons{{
        {UiButtonIds::ResidentialButton, "ui/toolbar/btn_res", UiButtonShape::Circle},
        {UiButtonIds::CommercialButton, "ui/toolbar/btn_com", UiButtonShape::Circle},
        {UiButtonIds::IndustrialButton, "ui/toolbar/btn_ind", UiButtonShape::Circle},
        {UiButtonIds::DezoneButton, "ui/toolbar/btn_dezone", UiButtonShape::Circle}
    }};
    constexpr float ZoningButtonSize = 48.0f;
    constexpr float ZoningButtonGap = 8.0f;

    constexpr std::array<ButtonDefinition, 6> TickerButtons{{
        {UiButtonIds::TickerPauseButton, "ui/ticker/btn_pause"},
        {UiButtonIds::TickerSpeedButton1, "ui/ticker/btn_tickersquare"},
        {UiButtonIds::TickerSpeedButton2, "ui/ticker/btn_tickersquare"},
        {UiButtonIds::TickerSpeedButton3, "ui/ticker/btn_tickersquare"},
        {UiButtonIds::TickerSpeedButton4, "ui/ticker/btn_tickersquare"},
        {UiButtonIds::TickerPlayButton, "ui/ticker/btn_play"}
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
        constexpr float count = static_cast<float>(ToolbarButtons.size());
        constexpr float stackHeight = count * ToolbarButtonHeight + (count - 1.0f) * ToolbarButtonGap;

        // Preserve the art's proportions and fit the whole stack above the map.
        const float scale = std::max(0.0f, std::min({layout.scale, area.w / ToolbarButtonWidth, area.h / stackHeight}));
        const float width = ToolbarButtonWidth * scale;
        const float height = ToolbarButtonHeight * scale;
        const float gap = ToolbarButtonGap * scale;
        const float x = area.x + (area.w - width) * 0.5f;
        float y = area.y;

        for (const auto& definition : ToolbarButtons)
        {
            if (auto* button = buttons.Find(definition.id))
            {
                button->SetBounds({x, y, width, height});
            }
            y += height + gap;
        }
    }

    void LayoutZoningButtons(UiButtons& buttons, const UiLayout& layout)
    {
        const auto* trigger = buttons.Find(UiButtonIds::ZonesButton);

        if (!trigger) 
        {
            return;
        }

        const UiRect& anchor = trigger->GetBounds();
        constexpr float count = static_cast<float>(ZoningButtons.size());
        constexpr float rowWidth = count * (ZoningButtonSize + ZoningButtonGap);
        const float margin = 4.0f * layout.scale;
        // Short windows shrink the toolbar buttons horizontally as well. Keep
        // every submenu icon outside the toolbar, not in that extra side margin.
        const float attachmentX = std::min(anchor.x, layout.toolbar.x);
        const float scale = std::max(0.0f, std::min(anchor.h / ToolbarButtonHeight, (attachmentX - margin) / rowWidth));
        const float size = ZoningButtonSize * scale;
        const float gap = ZoningButtonGap * scale;
        const float y = anchor.y + (anchor.h - size) * 0.5f;
        float x = attachmentX - gap - size;
        float left = anchor.x;

        for (const auto& definition : ZoningButtons)
        {
            if (auto* button = buttons.Find(definition.id)) 
            {
                button->SetBounds({x, y, size, size});
            }

            left = x;
            x -= size + gap;
        }

        // A continuous hit area includes the spaces between circular buttons.
        buttons.SetFlyoutBounds(UiButtonIds::ZonesButton, {left, anchor.y, anchor.x + anchor.w - left, anchor.h});
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
        AddButton(buttons, textures, definition);
    }

    for (const auto& definition : TickerButtons)
    {
        AddButton(buttons, textures, definition);
        // UiManager combines simulation selection with the ticker's group hover.
        buttons.Find(definition.id)->SetInteractionFeedbackEnabled(false);
    }

    std::array<UiButtonId, ZoningButtons.size()> children{};

    for (std::size_t i = 0; i < ZoningButtons.size(); ++i)
    {
        AddButton(buttons, textures, ZoningButtons[i]);
        children[i] = ZoningButtons[i].id;
    }

    buttons.AddFlyout(UiButtonIds::ZonesButton, children);
}

void LayoutUiButtons(UiButtons& buttons, UiMinimap& minimap, const UiLayout& layout)
{
    LayoutToolbarButtons(buttons, layout);
    LayoutZoningButtons(buttons, layout);
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
