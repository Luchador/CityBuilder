#pragma once
#include <cstdint>
#include <vector>

constexpr std::uint32_t MapSize = 256;
constexpr float MapExtent = static_cast<float>(MapSize);
constexpr std::uint32_t TerrainStride = MapSize + 1;
constexpr std::uint32_t TerrainVertexCount = TerrainStride * TerrainStride;

struct TerrainSettings
{
    std::uint32_t seed = 1337;
    float hillHeight = 8.0f;   // Highest point, in tile-length units. Range: 0..128.
    float hillSize = 32.0f;     // Broad noise wavelength in tiles. Range: 8..1024.
    float flatCoverage = 0.7f;  // Fraction of vertices flattened to Y=0. Range: 0..1.
};

// One shared height per tile corner. Retained by Application for future terrain
// queries/editing; render geometry is built from this data, not the other way round.
struct TerrainHeightmap
{
    std::vector<float> heights = std::vector<float>(TerrainVertexCount, 0.0f);

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
};
static_assert(sizeof(Vertex) == 24);

struct TerrainMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Vertex> lines;
};

TerrainHeightmap GenerateTerrain(const TerrainSettings& settings);
TerrainMesh BuildTerrain(const TerrainHeightmap& terrain);
