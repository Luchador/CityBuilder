#pragma once
#include "world.hpp"

#include <array>
#include <cstdint>
#include <vector>

struct TerrainColor
{
    float r, g, b; // Each component is in 0..1.
};

struct TerrainColorBand
{
    float upperElevation;
    // Blend toward the NEXT band's color in [upperElevation - transitionWidth,
    // upperElevation]. Zero means a hard boundary. The last band has no blend.
    float transitionWidth;
    TerrainColor color;
};

struct TerrainPalette
{
    // Colors are fully reached at the previous row's upper elevation; the blend is at the top of each band.
    std::array<TerrainColorBand, 7> bands{{
        // Upper elevation                          // Transition width     // Color
        {TerrainSeaLevel,                           0.5f,                   {0.70f, 0.70f, 0.66f}}, // Sand
        {1.0f,                                      1.0f,                   {0.37f, 0.44f, 0.37f}}, // Pale green
        {2.5f,                                      0.5f,                   {0.28f, 0.44f, 0.22f}}, // Light green
        {3.5f,                                      0.5f,                   {0.24f, 0.4f,  0.18f}}, // Medium green
        {8.0f,                                      4.0f,                   {0.10f, 0.32f, 0.14f}}, // Dark green
        {224.0f,                                    32.0f,                  {0.42f, 0.51f, 0.22f}}, // Yellow-green
        {TerrainMaxElevation,                       0.0f,                   {1.00f, 1.00f, 1.00f}}  // Snow.
    }};
};

// Samples the palette independently of mesh generation. Elevations beyond the
// palette endpoints use its endpoint colors. Invalid palettes/heights throw.
TerrainColor SampleTerrainColor(float height, const TerrainPalette& palette = {});

// Terrain Vertex struct. Y is elevation. 1 unit = 1 tile length.
struct Vertex
{
    float x, y, z;
    float nx = 0, ny = 1, nz = 0;

    // Fallback color; BuildTerrain assigns colors from its elevation palette.
    float r = 0.20f;
    float g = 0.58f;
    float b = 0.22f;
};
static_assert(sizeof(Vertex) == 36);

struct TerrainMesh {
    MapDimensions dimensions;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Grid lines reference the same vertices as the terrain triangles.
    std::vector<std::uint32_t> lineIndices;
};

TerrainHeightmap GenerateTerrain(const TerrainSettings& settings);
TerrainMesh BuildTerrain(const TerrainHeightmap& terrain, const TerrainPalette& palette = {});
