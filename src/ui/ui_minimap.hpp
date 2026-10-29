#pragma once

#include "ui.hpp"

class Texture;
class UiImageRenderer;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;

struct MinimapScreenPoint
{
    float x, y;
};

struct MinimapView
{
    // Same world-space camera values as the terrain shader. The aspect ratio
    // is the world viewport's pixel width / height, excluding the UI bars.
    float centerX = 0.0f, centerZ = 0.0f;
    float halfHeight = 0.0f, aspect = 1.0f;
    float mapExtent = 0.0f;
    unsigned int quarterTurns = 0;

    bool operator==(const MinimapView&) const = default;
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
    // Multiply a successful result by the current camera's GetMapExtent().
    // On a miss, the output arguments are left unchanged.
    bool TryScreenToMap(float x, float y, float& mapX, float& mapZ) const;
    MinimapScreenPoint MapToScreen(float mapX, float mapZ) const;

    // Append two opaque triangles to the existing UI mesh.
    void AppendGeometry(std::vector<UiVertex>& vertices) const;

    // Draw a map-coordinate texture into exactly the same diamond as picking.
    // Call after the panel background and before the red view outline/buttons.
    void DrawMap(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                 const Texture& image, float windowWidth, float windowHeight) const;

    // Camera footprint on the y=0 reference plane, in logical window units.
    // Bounds intentionally extend beyond the diamond when the view does.
    UiRect GetViewBounds(const MinimapView& view) const;

    // Clip the complete thickness of each red edge to the diamond. This does
    // not add new red edges along the diamond boundary.
    void AppendViewOutline(std::vector<UiVertex>& vertices, const MinimapView& view) const;

private:
    UiRect bounds{};
    UiRect contentBounds{};
    UiRect diamondBounds{};
};
