#include "ui_minimap.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    // A slightly taller diamond than the terrain's 33-degree projection,
    // similar to the SimCity 3000 minimap. This changes its shape, not its
    // map coverage: the full square map always fits inside the diamond.
    constexpr float DiamondHeightRatio = 1.0f;

    constexpr float DiamondRed = 0.30f;
    constexpr float DiamondGreen = 0.58f;
    constexpr float DiamondBlue = 0.28f;

    // Accept floating-point rounding at the edges, far less than one pixel.
    constexpr float EdgeTolerance = 0.00001f;

    void AppendRedRectangle(std::vector<UiVertex>& vertices, UiRect rect)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f)
        {
            return;
        }

        const UiVertex a{rect.x,          rect.y,          1, 0, 0, 1};
        const UiVertex b{rect.x + rect.w, rect.y,          1, 0, 0, 1};
        const UiVertex c{rect.x,          rect.y + rect.h, 1, 0, 0, 1};
        const UiVertex d{rect.x + rect.w, rect.y + rect.h, 1, 0, 0, 1};

        vertices.insert(vertices.end(), {a, b, c, b, d, c});
    }
}

void UiMinimap::SetLayout(const UiLayout& layout)
{
    bounds = layout.minimapPanel;
    contentBounds = layout.minimapContent;

    const float width = std::min(std::max(0.0f, contentBounds.w), std::max(0.0f, contentBounds.h) / DiamondHeightRatio);
    const float height = width * DiamondHeightRatio;

    diamondBounds = {contentBounds.x + (contentBounds.w - width) * 0.5f, contentBounds.y + (contentBounds.h - height) * 0.5f, width, height};
}

const UiRect& UiMinimap::GetBounds() const
{
    return bounds;
}

const UiRect& UiMinimap::GetContentBounds() const
{
    return contentBounds;
}

const UiRect& UiMinimap::GetDiamondBounds() const
{
    return diamondBounds;
}

bool UiMinimap::Contains(float x, float y) const
{
    float mapX = 0.0f, mapZ = 0.0f;
    return TryScreenToMap(x, y, mapX, mapZ);
}

bool UiMinimap::TryScreenToMap(float x, float y, float& mapX, float& mapZ) const
{
    if (diamondBounds.w <= 0.0f || diamondBounds.h <= 0.0f || !std::isfinite(x) || !std::isfinite(y))
    {
        return false;
    }

    const float halfWidth = diamondBounds.w * 0.5f;
    const float halfHeight = diamondBounds.h * 0.5f;
    const float centerX = diamondBounds.x + halfWidth;
    const float centerY = diamondBounds.y + halfHeight;
    const float dx = (x - centerX) / halfWidth;
    const float dy = (y - centerY) / halfHeight;

    // A bounding rectangle alone would incorrectly accept the empty corners.
    if (std::abs(dx) + std::abs(dy) > 1.0f + EdgeTolerance)
    {
        return false;
    }

    // Inverse of MapToScreen. Match the terrain: +X goes down-right,
    // +Z goes down-left, and the origin is the top corner.
    mapX = std::clamp((1.0f + dy + dx) * 0.5f, 0.0f, 1.0f);
    mapZ = std::clamp((1.0f + dy - dx) * 0.5f, 0.0f, 1.0f);

    return true;
}

MinimapScreenPoint UiMinimap::MapToScreen(float mapX, float mapZ) const
{
    const float halfWidth = diamondBounds.w * 0.5f;
    const float halfHeight = diamondBounds.h * 0.5f;

    return {diamondBounds.x + halfWidth + (mapX - mapZ) * halfWidth, diamondBounds.y + halfHeight + (mapX + mapZ - 1.0f) * halfHeight};
}

void UiMinimap::AppendGeometry(std::vector<UiVertex>& vertices) const
{
    if (diamondBounds.w <= 0.0f || diamondBounds.h <= 0.0f)
    {
        return;
    }

    const auto vertex = [this](float mapX, float mapZ)
    {
        const MinimapScreenPoint point = MapToScreen(mapX, mapZ);
        return UiVertex{point.x, point.y, DiamondRed, DiamondGreen, DiamondBlue, 1.0f};
    };

    const UiVertex top = vertex(0.0f, 0.0f);
    const UiVertex right = vertex(1.0f, 0.0f);
    const UiVertex bottom = vertex(1.0f, 1.0f);
    const UiVertex left = vertex(0.0f, 1.0f);

    vertices.insert(vertices.end(), {top, right, bottom, top, bottom, left});
}

void UiMinimap::AppendViewOutline(std::vector<UiVertex>& vertices) const
{
    if (diamondBounds.w <= 0.0f || diamondBounds.h <= 0.0f)
    {
        return;
    }

    const MinimapScreenPoint center = MapToScreen(0.5f, 0.5f);

    // Size relative to the diamond's bounding rectangle.
    const float width = diamondBounds.w * 0.40f;
    const float height = diamondBounds.h * 0.30f;

    const UiRect box{center.x - width * 0.5f, center.y - height * 0.5f, width, height};

    const float thickness = std::min({2.0f, box.w * 0.5f, box.h * 0.5f});

    // Top edge.
    AppendRedRectangle(vertices, {box.x, box.y, box.w, thickness});

    // Bottom edge.
    AppendRedRectangle(vertices, {box.x, box.y + box.h - thickness, box.w, thickness});

    // Left edge, between the top and bottom strips.
    AppendRedRectangle(vertices, {box.x, box.y + thickness, thickness, box.h - 2.0f * thickness});

    // Right edge.
    AppendRedRectangle(vertices, {box.x + box.w - thickness, box.y + thickness, thickness, box.h - 2.0f * thickness});
}
