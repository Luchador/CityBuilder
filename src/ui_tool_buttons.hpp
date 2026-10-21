#pragma once

#include "ui_button.hpp"
#include <cstddef>
#include <span>

// Shared by all tool flyouts. All sizes are before UI scaling.
inline constexpr float UiToolButtonSize = 64.0f;
inline constexpr float UiToolButtonGap = 8.0f;

struct UiToolButtonDefinition
{
    UiButtonId id;
    const char* tooltip;
    const char* imageStem; // Relative to assets/, without .png. One icon per tool.
};

struct UiToolMenuDefinition
{
    UiButtonId trigger;
    std::span<const UiToolButtonDefinition> buttons;
    std::size_t columns = 1; // Left to right, then top to bottom. Defaults to a vertical list.
};

void AddToolFlyout(UiButtons& buttons, TextureManager& textures, UiButtonId trigger,
                   std::span<const UiToolButtonDefinition> definitions, UiButtonShape shape);
void LayoutToolFlyouts(UiButtons& buttons, const UiLayout& layout, UiButtonId trigger,
                       std::span<const UiToolButtonDefinition> categories,
                       std::span<const UiToolMenuDefinition> menus);
