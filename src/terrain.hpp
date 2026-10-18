#pragma once
#include <array>
#include <cstdint>
#include <vector>

constexpr std::uint32_t MapSize = 256;
constexpr float MapExtent = static_cast<float>(MapSize);
constexpr std::uint32_t TerrainStride = MapSize + 1;
constexpr std::uint32_t TerrainVertexCount = TerrainStride * TerrainStride;

// Allowed elevation increments
constexpr float TerrainHeightStep = 0.25f;
constexpr float TerrainSeaLevel = 0.0f;
constexpr float TerrainMaxElevation = 256.0f;

constexpr bool IsWaterElevation(float height)
{
    return height < TerrainSeaLevel;
}

struct LakeSettings
{
    std::uint32_t count = 6;    // Maximum lakes; fewer if there isn't enough flat land. Range: 0..64.
    float minRadius = 10.0f;    // Radius in tiles, before shoreline variation. Range: 4..64.
    float maxRadius = 22.0f;    // Range: minRadius..64.
    float depth = 3.0f;         // Maximum depth below sea level; snapped down to a height step. Range: 0..256.
};

struct TerrainSettings
{
    std::uint32_t seed = 1337;
    float hillHeight = 6.0f;   // Height cap in tile units; rounded down to a height step. Range: 0..256.
    float hillSize = 32.0f;     // Broad noise wavelength in tiles. Range: 8..1024.
    float flatCoverage = 0.7f;  // Fraction flattened to Y=0 before height snapping. Range: 0..1.
    LakeSettings lakes;         // Second pass; count=0 or depth=0 keeps the original landscape.
};

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
    std::array<TerrainColorBand, 6> bands{{
        // Upper elevation                          // Transition width     // Color
        {TerrainSeaLevel,                           1.0f,                   {0.06f, 0.28f, 0.52f}}, // Water
        {0.5f,                                      0.25f,                  {0.40f, 0.50f, 0.40f}}, // Light green
        {1.5f,                                      1.0f,                   {0.24f, 0.4f,  0.18f}}, // Medium green
        {8.0f,                                      4.0f,                   {0.10f, 0.32f, 0.14f}}, // Dark green
        {224.0f,                                    32.0f,                  {0.42f, 0.51f, 0.22f}}, // Yellow-green
        {TerrainMaxElevation,                       0.0f,                   {1.00f, 1.00f, 1.00f}}  // Snow.
    }};
};

// Samples the palette independently of mesh generation. Elevations beyond the
// palette endpoints use its endpoint colors. Invalid palettes/heights throw.
TerrainColor SampleTerrainColor(float height, const TerrainPalette& palette = {});

// One shared height per tile corner. Retained by Application for future terrain
// queries/editing; render geometry is built from this data, not the other way round.
struct TerrainHeightmap
{
    std::vector<float> heights = std::vector<float>(TerrainVertexCount, 0.0f);

    void Validate() const;

    // x and z must be in 0..MapSize, inclusive.
    float Height(std::uint32_t x, std::uint32_t z) const
    {
        return heights[z * TerrainStride + x];
    }
};

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
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Vertex> lines;
};

TerrainHeightmap GenerateTerrain(const TerrainSettings& settings);
TerrainMesh BuildTerrain(const TerrainHeightmap& terrain, const TerrainPalette& palette = {});
