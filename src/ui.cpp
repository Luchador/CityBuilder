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

    void AddButton(UiButtons& buttons, TextureManager& textures, const ButtonDefinition& definition)
    {
        const std::string stem = definition.imageStem;
    
        auto& button = buttons.Add(definition.id, {
            &textures.Load(stem + "_normal.png"),
            &textures.Load(stem + "_hov.png"),
            &textures.Load(stem + "_pressed.png")
        });
    
        button.SetShape(definition.shape);
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
}

void CreateUiButtons(UiButtons& buttons, TextureManager& textures)
{
    for (const auto& definition : MinimapButtons) AddButton(buttons, textures, definition);
    for (const auto& definition : ToolbarButtons) AddButton(buttons, textures, definition);
}

void LayoutUiButtons(UiButtons& buttons, UiMinimap& minimap, const UiLayout& layout)
{
    LayoutToolbarButtons(buttons, layout);

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
    if (buttons.HasCapture()) SDL_CaptureMouse(false);
    buttons.CancelInteraction();
}

void SetUiMinimized(UiSettings& settings, UiButtons& buttons, bool minimized)
{
    CancelUiButtonInteraction(buttons);
    buttons.ShowOnly(minimized ? std::optional<UiButtonId>{UiButtonIds::MinimizeUIButton} : std::nullopt);
    settings.minimized = minimized;
}

UiButtonResult HandleUiButtonEvent(SDL_Window* window, const SDL_Event& event,
                                   const UiSettings& settings, UiButtons& buttons, UiMinimap& minimap)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
        event.type == SDL_EVENT_WINDOW_HIDDEN || event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        CancelUiButtonInteraction(buttons);
        return {}; // Let camera panning also receive these events.
    }

    if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && !buttons.HasCapture())
    {
        buttons.CancelInteraction();
    }

    const bool pointerEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_WHEEL;

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
            return {buttons.HasCapture() || buttons.BlocksPointer(event.button.x, event.button.y), {}};
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
        return {buttons.HasCapture(), {}};
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
