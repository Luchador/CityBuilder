#pragma once

#include "ui.hpp"

struct MinimapScreenPoint
{
    float x, y;
};

class UiMinimap
{
public:
    void SetLayout(const UiLayout& layout);

    // Panel and interior rectangles, in logical window coordinates.
    const UiRect& GetBounds() const;
    const UiRect& GetContentBounds() const;
    const UiRect& GetDiamondBounds() const;

    // Only the diamond is clickable; the surrounding panel is not map area.
    bool Contains(float x, float y) const;

    // Map coordinates are normalized: X/Z each range from 0 to 1.
    // Multiply a successful result by MapExtent to get world coordinates.
    // On a miss, the output arguments are left unchanged.
    bool TryScreenToMap(float x, float y, float& mapX, float& mapZ) const;
    MinimapScreenPoint MapToScreen(float mapX, float mapZ) const;

    // Append two opaque triangles to the existing UI mesh.
    void AppendGeometry(std::vector<UiVertex>& vertices) const;

private:
    UiRect bounds{};
    UiRect contentBounds{};
    UiRect diamondBounds{};
};
