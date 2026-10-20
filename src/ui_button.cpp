#include "ui_button.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

UiButton::UiButton(UiButtonTextures images)
{
    SetTextures(images);
}

void UiButton::SetBounds(UiRect rectangle)
{
    if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) || !std::isfinite(rectangle.w) || !std::isfinite(rectangle.h) || rectangle.w < 0 || rectangle.h < 0)
    {
        throw std::invalid_argument("Button bounds must be finite with nonnegative dimensions.");
    }

    if (bounds.x == rectangle.x && bounds.y == rectangle.y && bounds.w == rectangle.w && bounds.h == rectangle.h) 
    {
        return;
    }

    bounds = rectangle;

    CancelPress();
}

void UiButton::SetHitInsets(UiButtonInsets insets)
{
    for (float value : {insets.left, insets.top, insets.right, insets.bottom})
    {
        if (!std::isfinite(value) || value < 0) 
        {
            throw std::invalid_argument("Button hit insets must be finite and nonnegative.");
        }
    }

    if (hitInsets.left == insets.left && hitInsets.top == insets.top && hitInsets.right == insets.right && hitInsets.bottom == insets.bottom) 
    {
        return;
    }

    hitInsets = insets;

    CancelPress();
}

UiRect UiButton::GetHitBounds() const
{
    return {bounds.x + hitInsets.left, bounds.y + hitInsets.top, std::max(0.0f, bounds.w - hitInsets.left - hitInsets.right), std::max(0.0f, bounds.h - hitInsets.top - hitInsets.bottom)};
}

void UiButton::SetTextures(UiButtonTextures images)
{
    if (!images.normal) 
    {
        throw std::invalid_argument("A button requires a normal texture.");
    }

    textures = images;
}

void UiButton::SetShape(UiButtonShape value)
{
    if (shape == value) 
    {
        return;
    }

    shape = value;

    CancelPress();
}

void UiButton::SetCustomHitTest(HitTestFunction function)
{
    customHitTest = std::move(function);
    shape = UiButtonShape::Custom;

    CancelPress();
}

void UiButton::SetEnabled(bool value)
{
    enabled = value;

    if (!enabled) 
    {
        CancelPress();
    }
}

void UiButton::SetVisible(bool value)
{
    visible = value;

    if (!visible) 
    {
        CancelPress();
    }
}

bool UiButton::HitTest(float x, float y) const
{
    if (!visible || !std::isfinite(x) || !std::isfinite(y)) 
    {
        return false;
    }

    const UiRect hit = GetHitBounds();

    if (hit.w <= 0 || hit.h <= 0) 
    {
        return false;
    }

    if (shape == UiButtonShape::Circle)
    {
        const float dx = x - (hit.x + hit.w * 0.5f);
        const float dy = y - (hit.y + hit.h * 0.5f);
        const float radius = std::min(hit.w, hit.h) * 0.5f;

        return dx * dx + dy * dy <= radius * radius;
    }

    if (!hit.Contains(x, y)) 
    {
        return false;
    }

    if (shape == UiButtonShape::Custom)
    {
        return customHitTest && customHitTest((x - hit.x) / hit.w, (y - hit.y) / hit.h);
    }

    return true;
}

bool UiButton::PointerDown(float x, float y)
{
    if (pressed || !enabled || !HitTest(x, y)) 
    {
        return false;
    }

    pressed = true;
    SetHovered(true);
    ++hoverRevision;

    return true;
}

void UiButton::PointerMove(float x, float y)
{
    SetHovered(enabled && HitTest(x, y));
}

bool UiButton::PointerUp(float x, float y)
{
    const bool inside = enabled && HitTest(x, y);
    const bool clicked = pressed && inside;

    pressed = false;
    SetHovered(inside);
    ++hoverRevision;

    return clicked;
}

void UiButton::CancelPress()
{
    pressed = false;
    SetHovered(false);
    highlighted = false;
}

UiButtonState UiButton::GetState() const
{
    if (!enabled) 
    {
        return UiButtonState::Normal;
    }

    if (highlighted)
    {
        return UiButtonState::Highlighted;
    }

    if (selected || (interactionFeedback && pressed && hovered))
    {
        return UiButtonState::Pressed;
    }

    return interactionFeedback && hovered ? UiButtonState::Highlighted : UiButtonState::Normal;
}

const Texture* UiButton::GetTexture() const
{
    const UiButtonState state = GetState();

    if (state == UiButtonState::Pressed && textures.pressed) 
    {
        return textures.pressed;
    }

    if (state == UiButtonState::Highlighted && textures.highlighted) 
    {
        return textures.highlighted;
    }

    return textures.normal;
}
