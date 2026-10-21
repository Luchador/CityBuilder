#pragma once

#include "tool.hpp"

class Camera;
struct TerrainHeightmap;

struct PickingViewport
{
    // The exact pixel rectangle used by SDL_SetGPUViewport for the world pass.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    // Swapchain pixel dimensions divided by SDL window-coordinate dimensions.
    float pixelsPerWindowX = 1.0f;
    float pixelsPerWindowY = 1.0f;
};

struct TerrainPickOptions
{
    // Pick visible lake surfaces. Set false to target the terrain underneath.
    bool includeWater = true;
    // Supply the renderer's skirt bottom to reject hits hidden behind rock.
    // Empty means the caller is rendering no skirt.
    std::optional<float> skirtBottomY;
};

// screenX/Y are SDL window coordinates, not physical pixels. Returns the
// closest front-facing terrain/water hit, or nullopt outside the viewport/map
// or behind the skirt. UI occlusion and camera input capture are caller policy.
// Uses the same projection, depth clip, triangle diagonal, and shoreline as
// the world shaders. Terrain must have been validated when created/edited.
// Walks only ray-crossed grid cells (O(map side length), no heap allocation).
// Shared tile edges use floor(X/Z); the outermost edge belongs to the last tile.
std::optional<ToolHit> PickMapTile(const TerrainHeightmap& terrain, const Camera& camera,
                                 const PickingViewport& viewport, float screenX, float screenY,
                                 const TerrainPickOptions& options = {});
