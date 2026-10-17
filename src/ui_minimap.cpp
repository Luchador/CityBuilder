#include "ui_minimap.hpp"
#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float DiamondRed = 0.30f;
    constexpr float DiamondGreen = 0.58f;
    constexpr float DiamondBlue = 0.28f;

    // Accept floating-point rounding at the edges, far less than one pixel.
    constexpr float EdgeTolerance = 0.00001f;
    constexpr float ViewOutlineThickness = 2.0f; // Logical window units.

    void AppendClippedRedRectangle(std::vector<UiVertex>& vertices, UiRect rect, UiRect diamond)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f)
        {
            return;
        }

        const float halfWidth = diamond.w * 0.5f;
        const float halfHeight = diamond.h * 0.5f;
        const float centerX = diamond.x + halfWidth;
        const float centerY = diamond.y + halfHeight;

        const auto normalized = [=](float x, float y)
        {
            return MinimapScreenPoint{(x - centerX) / halfWidth, (y - centerY) / halfHeight};
        };

        std::vector<MinimapScreenPoint> polygon{
            normalized(rect.x, rect.y),
            normalized(rect.x + rect.w, rect.y),
            normalized(rect.x + rect.w, rect.y + rect.h),
            normalized(rect.x, rect.y + rect.h)
        };

        std::vector<MinimapScreenPoint> clipped;
        clipped.reserve(8);

        // Sutherland-Hodgman clipping against the diamond's four half-planes.
        // Clip the filled stroke, not just its centerline, so its thickness
        // cannot leak outside a sloped edge.
        constexpr MinimapScreenPoint normals[] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

        for (const auto normal : normals)
        {
            if (polygon.empty()) 
            {
                return;
            }
    
            clipped.clear();

            const auto distance = [normal](MinimapScreenPoint p)
            {
                return 1.0f - normal.x * p.x - normal.y * p.y;
            };

            auto previous = polygon.back();
            float previousDistance = distance(previous);

            for (const auto current : polygon)
            {
                const float currentDistance = distance(current);
    
                if ((previousDistance >= 0.0f) != (currentDistance >= 0.0f))
                {
                    const float t = previousDistance / (previousDistance - currentDistance);
    
                    clipped.push_back({previous.x + (current.x - previous.x) * t, previous.y + (current.y - previous.y) * t});
                }

                if (currentDistance >= 0.0f) 
                {
                    clipped.push_back(current);
                }

                previous = current;
                previousDistance = currentDistance;
            }

            polygon.swap(clipped);
        }

        if (polygon.size() < 3) 
        {
            return;
        }

        const auto vertex = [=](MinimapScreenPoint p)
        {
            return UiVertex{centerX + p.x * halfWidth, centerY + p.y * halfHeight, 1, 0, 0, 1};
        };

        const UiVertex first = vertex(polygon[0]);

        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
        {
            const UiVertex b = vertex(polygon[i]);
            const UiVertex c = vertex(polygon[i + 1]);
            const float area = (b.x - first.x) * (c.y - first.y) - (b.y - first.y) * (c.x - first.x);

            if (area > 0.0f) 
            {
                vertices.insert(vertices.end(), {first, b, c});
            }
        }
    }
}

void UiMinimap::SetLayout(const UiLayout& layout)
{
    bounds = layout.minimapPanel;
    contentBounds = layout.minimapContent;

    const float width = std::min(std::max(0.0f, contentBounds.w), std::max(0.0f, contentBounds.h));
    const float height = width;

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

UiRect UiMinimap::GetViewBounds(const MinimapView& view) const
{
    if (diamondBounds.w <= 0.0f || diamondBounds.h <= 0.0f || !std::isfinite(view.centerX) || !std::isfinite(view.centerZ) || !std::isfinite(view.halfHeight) || !std::isfinite(view.aspect) || !std::isfinite(view.mapExtent) || view.halfHeight <= 0.0f || view.aspect <= 0.0f || view.mapExtent <= 0.0f)
    {
        return {};
    }

    // Invert the same sea-level projection used for camera panning. Do not
    // clamp these world coordinates: that would shrink the camera footprint.
    const float halfWidth = view.halfHeight * view.aspect;
    const GroundPan upperLeft = ScreenToGroundPan(-halfWidth, -view.halfHeight);
    const GroundPan lowerRight = ScreenToGroundPan(halfWidth, view.halfHeight);
    const auto topLeft = MapToScreen((view.centerX + upperLeft.x) / view.mapExtent, (view.centerZ + upperLeft.z) / view.mapExtent);

    const auto bottomRight = MapToScreen((view.centerX + lowerRight.x) / view.mapExtent, (view.centerZ + lowerRight.z) / view.mapExtent);
    const UiRect box{topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};

    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) || !std::isfinite(box.h))
    {
        return {};
    }

    return box;
}

void UiMinimap::AppendViewOutline(std::vector<UiVertex>& vertices, const MinimapView& view) const
{
    const UiRect box = GetViewBounds(view);

    if (box.w <= 0.0f || box.h <= 0.0f) 
    {
        return;
    }

    const float thickness = std::min({ViewOutlineThickness, box.w, box.h});
    const float half = thickness * 0.5f;

    // Center the red stroke on the actual viewport boundary. Each original
    // edge is clipped independently; a view containing the entire map can
    // correctly have no visible outline because all four edges are off-map.
    AppendClippedRedRectangle(vertices, {box.x - half, box.y - half, box.w + thickness, thickness}, diamondBounds);
    AppendClippedRedRectangle(vertices, {box.x - half, box.y + box.h - half, box.w + thickness, thickness}, diamondBounds);
    AppendClippedRedRectangle(vertices, {box.x - half, box.y + half, thickness, box.h - thickness}, diamondBounds);
    AppendClippedRedRectangle(vertices, {box.x + box.w - half, box.y + half, thickness, box.h - thickness}, diamondBounds);
}
