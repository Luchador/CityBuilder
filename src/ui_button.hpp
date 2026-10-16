#pragma once

#include "ui.hpp"
#include <functional>

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
    void SetShape(UiButtonShape value);
    // Receives normalized coordinates in the inset hit rectangle, from 0 to 1.
    // Installing a function also selects Custom. An empty function never hits.
    void SetCustomHitTest(HitTestFunction function);

    void SetEnabled(bool value);
    void SetVisible(bool value);
    void SetSelected(bool value) { selected = value; }
    bool IsEnabled() const { return enabled; }
    bool IsVisible() const { return visible; }
    bool IsSelected() const { return selected; }
    bool IsHovered() const { return hovered; }
    bool HasPress() const { return pressed; }

    // A visible disabled button still has a hit area, to block clicks through it.
    bool HitTest(float x, float y) const;
    bool PointerDown(float x, float y); // True if this button accepted the press.
    void PointerMove(float x, float y);
    void PointerLeave() { hovered = false; }
    bool PointerUp(float x, float y);   // True once for a completed click.
    void CancelPress();

    UiButtonState GetState() const;
    const Texture* GetTexture() const;

private:
    UiRect bounds{};
    UiButtonInsets hitInsets{};
    UiButtonTextures textures{};
    UiButtonShape shape = UiButtonShape::Rectangle;
    HitTestFunction customHitTest;
    bool enabled = true, visible = true, selected = false;
    bool hovered = false, pressed = false;
};
