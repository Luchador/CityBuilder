#include "ui_tool_buttons.hpp"
#include "ui_buttons.hpp"
#include "texture.hpp"

#include <algorithm>
#include <string>

void AddToolFlyout(UiButtons& buttons, TextureManager& textures, UiButtonId trigger,
                   std::span<const UiToolButtonDefinition> definitions, UiButtonShape shape)
{
    if (definitions.empty()) return;
    const std::string stem = shape == UiButtonShape::Circle ?
        "ui/toolbar/btn_circle" : "ui/toolbar/btn_square";
    // These are grayscale coverage masks, not alpha-channel images. Data avoids
    // color-space conversion; TextureManager caches the six shared skin images.
    const UiButtonSkin skin{
        &textures.Load(stem + "_opacity.png", {TextureContent::Data}),
        &textures.Load(stem + "_hov.png"),
        &textures.Load(stem + "_pressed.png")
    };
    std::vector<UiButtonId> children;
    for (const auto& definition : definitions)
    {
        auto& button = buttons.Add(definition.id, {
            &textures.Load(std::string(definition.imageStem) + ".png")
        });
        button.SetSkin(skin);
        button.SetShape(shape); // Rectangular hit testing is sufficient for rounded squares.
        button.SetTooltip(definition.tooltip);
        button.SetTooltipDelayMs(0);
        children.push_back(definition.id);
    }
    buttons.AddFlyout(trigger, children);
}

void LayoutToolFlyouts(UiButtons& buttons, const UiLayout& layout, UiButtonId triggerId,
                       std::span<const UiToolButtonDefinition> categories,
                       std::span<const UiToolMenuDefinition> menus)
{
    const auto* trigger = buttons.Find(triggerId);
    if (!trigger || categories.empty()) return;
    const auto anchor = trigger->GetBounds();
    const float margin = 4.0f * layout.scale;
    const float attachmentX = std::min(anchor.x, layout.toolbar.x);
    const float categoryY = anchor.y;
    const float bottom = std::min(layout.height - margin, layout.bottomBar.y - margin);
    std::size_t mostRows = 0;
    std::size_t widestMenu = 1;
    for (const auto& menu : menus)
    {
        if (menu.buttons.empty()) continue;
        const auto columns = std::clamp(menu.columns, std::size_t{1}, menu.buttons.size());
        const auto rows = 1 + (menu.buttons.size() - 1) / columns;
        mostRows = std::max(mostRows, rows);
        widestMenu = std::max(widestMenu, columns);
    }

    // Keep square proportions, shrinking only when the UI scale or available
    // space requires it. The toolbar's shorter buttons do not cap this at 50px.
    const float rowWidth = static_cast<float>(std::max(categories.size(), widestMenu)) * (UiToolButtonSize + UiToolButtonGap);
    const float columnHeight = UiToolButtonSize + static_cast<float>(mostRows) * (UiToolButtonSize + UiToolButtonGap);
    const float scale = std::max(0.0f, std::min({layout.scale,
        (attachmentX - margin) / rowWidth, (bottom - categoryY) / columnHeight}));
    const float size = UiToolButtonSize * scale, gap = UiToolButtonGap * scale;
    float x = attachmentX - gap - size;
    float left = anchor.x;
    for (const auto& category : categories)
    {
        buttons.Find(category.id)->SetBounds({x, categoryY, size, size});
        left = x;
        x -= size + gap;
    }
    // Stop the corridor at the toolbar button's left edge. A 64px row can be
    // taller than its toolbar trigger; extending it across the toolbar would
    // swallow clicks on the next toolbar button at small resolutions.
    buttons.SetFlyoutBounds(triggerId, {left, categoryY, anchor.x - left, size});

    for (const auto& menu : menus)
    {
        if (menu.buttons.empty()) continue;
        const auto parent = buttons.Find(menu.trigger)->GetBounds();
        const auto columns = std::clamp(menu.columns, std::size_t{1}, menu.buttons.size());
        const auto rows = 1 + (menu.buttons.size() - 1) / columns;
        const float gridWidth = static_cast<float>(columns) * size + static_cast<float>(columns - 1) * gap;
        // Center the grid beneath its category, then keep it within the screen
        // margin and outside the toolbar. Single-column menus stay aligned.
        const float centeredX = parent.x + (parent.w - gridWidth) * 0.5f;
        const float x = std::max(margin, std::min(centeredX, attachmentX - gap - gridWidth));
        const float top = parent.y + parent.h + gap;
        for (std::size_t i = 0; i < menu.buttons.size(); ++i)
        {
            const float childX = x + static_cast<float>(i % columns) * (size + gap);
            const float childY = top + static_cast<float>(i / columns) * (size + gap);
            buttons.Find(menu.buttons[i].id)->SetBounds({childX, childY, size, size});
        }
        // Include gaps and unused final-row cells so clicks never pass through
        // an open grid to world tools or controls beneath it.
        const float bottomY = top + static_cast<float>(rows) * (size + gap) - gap;
        buttons.SetFlyoutBounds(menu.trigger, {x, parent.y, gridWidth, bottomY - parent.y});
    }
}
