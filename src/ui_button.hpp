#pragma once

#include "ui.hpp"
#include <functional>
#include <string>
#include <string_view>

class Texture;

enum class UiButtonShape { Rectangle, Circle, Custom };
enum class UiButtonState { Normal, Highlighted, Pressed };

struct UiButtonTextures
{
    // Borrowed from TextureManager, which must outlive the button.
    const Texture* normal = nullptr;
    const Texture* highlighted = nullptr;
    const Texture* pressed = nullptr;
};

struct UiButtonInsets
{
    float left = 0, top = 0, right = 0, bottom = 0;
};

// Optional shared artwork, composited over the normal icon by the tool shader.
// The opacity image stores coverage in its red channel (white = visible).
struct UiButtonSkin
{
    const Texture* opacity = nullptr;
    const Texture* highlighted = nullptr;
    const Texture* pressed = nullptr;
};

class UiButton
{
public:
    using HitTestFunction = std::function<bool(float u, float v)>;

    explicit UiButton(UiButtonTextures textures);
    UiButton(const UiButton&) = delete;
    UiButton& operator=(const UiButton&) = delete;

    void SetBounds(UiRect rectangle);
    const UiRect& GetBounds() const { return bounds; }
    void SetHitInsets(UiButtonInsets insets);
    UiRect GetHitBounds() const;
    void SetTextures(UiButtonTextures images);
    void SetSkin(UiButtonSkin value);
    const UiButtonSkin& GetSkin() const { return skin; }
    // Empty text disables the tooltip. Text is owned; temporary strings are safe.
    void SetTooltip(std::string_view text) { tooltip = text; }
    const std::string& GetTooltip() const { return tooltip; }
    std::uint64_t GetHoverRevision() const { return hoverRevision; }
    void SetShape(UiButtonShape value);
    // Receives normalized coordinates in the inset hit rectangle, from 0 to 1.
    // Installing a function also selects Custom. An empty function never hits.
    void SetCustomHitTest(HitTestFunction function);

    void SetEnabled(bool value);
    void SetVisible(bool value);
    void SetSelected(bool value) { selected = value; }
    // A visual highlight can include other buttons in a group. It takes priority
    // over selection without changing the button's actual pointer/click state.
    void SetHighlighted(bool value) { highlighted = value; }
    // Turn off hover/held artwork for buttons whose selection displays game state.
    // Hit testing, capture, and click handling still work normally.
    void SetInteractionFeedbackEnabled(bool value) { interactionFeedback = value; }
    bool IsEnabled() const { return enabled; }
    bool IsVisible() const { return visible; }
    bool IsSelected() const { return selected; }
    bool IsHovered() const { return hovered; }
    bool HasPress() const { return pressed; }

    // A visible disabled button still has a hit area, to block clicks through it.
    bool HitTest(float x, float y) const;
    bool PointerDown(float x, float y); // True if this button accepted the press.
    void PointerMove(float x, float y);
    void PointerLeave() { SetHovered(false); }
    bool PointerUp(float x, float y);   // True once for a completed click.
    void CancelPress();

    UiButtonState GetState() const;
    const Texture* GetTexture() const;

private:
    void SetHovered(bool value)
    {
        if (hovered != value) { hovered = value; ++hoverRevision; }
    }
    std::string tooltip;
    std::uint64_t hoverRevision = 0;
    UiRect bounds{};
    UiButtonInsets hitInsets{};
    UiButtonTextures textures{};
    UiButtonSkin skin{};
    UiButtonShape shape = UiButtonShape::Rectangle;
    HitTestFunction customHitTest;
    bool enabled = true, visible = true, selected = false;
    bool hovered = false, pressed = false;
    bool interactionFeedback = true;
    bool highlighted = false;
};
