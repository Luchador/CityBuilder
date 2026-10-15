#include "ui_minimap.hpp"

void UiMinimap::SetLayout(const UiLayout& layout)
{
    bounds = layout.minimapPanel;
    contentBounds = layout.minimapContent;
}

const UiRect& UiMinimap::GetBounds() const
{
    return bounds;
}

const UiRect& UiMinimap::GetContentBounds() const
{
    return contentBounds;
}

bool UiMinimap::Contains(float x, float y) const
{
    return bounds.Contains(x, y);
}