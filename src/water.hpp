#pragma once
#include "terrain.hpp"

// Independent of the terrain palette and lighting, ready for a water material.
inline constexpr TerrainColor WaterColor{0.06f, 0.28f, 0.52f};

struct WaterVertex
{
    float x, y, z;       // Surface position; y is always TerrainSeaLevel.
    float waterDepth;    // Sea level minus terrain height at this corner; may be negative on shore.
};
static_assert(sizeof(WaterVertex) == 16);

struct WaterMesh
{
    std::vector<WaterVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// One quad (two triangles) per tile with at least one corner BELOW sea level.
// Adjacent water tiles share vertices. All-dry maps produce an empty mesh.
WaterMesh BuildWater(const TerrainHeightmap& terrain);
