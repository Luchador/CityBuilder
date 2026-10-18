#pragma once

#include "terrain.hpp"

#include <span>

// CPU image in map coordinates: columns are +X, rows are +Z. Keeping this
// independent of the panel layout makes it reusable at any minimap size.
struct MinimapImage
{
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// Optional tile colors for future zones/roads. Opacity 0 keeps the terrain.
// A supplied array has MapSize * MapSize entries, indexed by z * MapSize + x.
// Water remains visible above this land overlay.
struct MinimapTileOverlay
{
    TerrainColor color{0, 0, 0};
    float opacity = 0.0f;
};

// Use the mesh returned by BuildTerrain, including its actual palette colors.
// Sample the same two triangles and sea-level shoreline as terrain/water.
// Colors show elevation without directional lighting, grid lines, or skirts.
MinimapImage BuildMinimapImage(const TerrainMesh& terrain,
                              std::span<const MinimapTileOverlay> overlays = {});
