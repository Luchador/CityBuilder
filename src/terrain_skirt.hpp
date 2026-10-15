#pragma once
#include "terrain.hpp"

inline constexpr std::uint32_t TerrainSkirtLayerCount = 40;

struct TerrainSkirtSettings
{
    float bottomY = -40.0f;          // Fixed world height, below every terrain edge. Range: -512..256.
    float thicknessExponent = 3.8f;  // 1 = equal layers; larger values thicken the lower layers. Range: 1..4.

    // Rock colors, ordered from the ground surface down to the bottom.
    std::array<TerrainColor, TerrainSkirtLayerCount> colors{{
        {0.48f, 0.41f, 0.34f}, {0.62f, 0.58f, 0.52f},
        {0.50f, 0.49f, 0.48f}, {0.57f, 0.47f, 0.37f},
        {0.43f, 0.45f, 0.47f}, {0.63f, 0.55f, 0.45f},
        {0.55f, 0.53f, 0.50f}, {0.44f, 0.37f, 0.31f},
        {0.66f, 0.62f, 0.55f}, {0.50f, 0.51f, 0.52f},
        {0.60f, 0.50f, 0.40f}, {0.46f, 0.44f, 0.42f},
        {0.62f, 0.60f, 0.57f}, {0.52f, 0.43f, 0.35f},
        {0.42f, 0.43f, 0.44f}, {0.65f, 0.56f, 0.45f},
        {0.56f, 0.55f, 0.53f}, {0.47f, 0.40f, 0.34f},
        {0.61f, 0.58f, 0.53f}, {0.49f, 0.50f, 0.51f},
        {0.58f, 0.48f, 0.39f}, {0.45f, 0.44f, 0.42f},
        {0.64f, 0.61f, 0.56f}, {0.53f, 0.46f, 0.38f},
        {0.44f, 0.46f, 0.47f}, {0.62f, 0.54f, 0.44f},
        {0.52f, 0.51f, 0.49f}, {0.46f, 0.39f, 0.33f},
        {0.60f, 0.58f, 0.54f}, {0.48f, 0.49f, 0.50f},
        {0.57f, 0.49f, 0.41f}, {0.43f, 0.42f, 0.40f},
        {0.61f, 0.59f, 0.55f}, {0.51f, 0.44f, 0.37f},
        {0.45f, 0.47f, 0.48f}, {0.59f, 0.52f, 0.43f},
        {0.53f, 0.52f, 0.50f}, {0.47f, 0.41f, 0.35f},
        {0.58f, 0.56f, 0.52f}, {0.44f, 0.45f, 0.46f}
    }};
};

struct TerrainSkirtVertex
{
    float x, y, z;
    float nx, ny, nz;
    float terrainHeight; // Height of the terrain edge directly above this vertex.
};
static_assert(sizeof(TerrainSkirtVertex) == 28);

struct TerrainSkirtMesh
{
    std::vector<TerrainSkirtVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// float4 packing matches the fragment shader's uniform buffer layout.
struct alignas(16) TerrainSkirtUniforms
{
    std::array<float, 4> settings; // bottomY, inverse thickness exponent, layer count, unused
    std::array<std::array<float, 4>, TerrainSkirtLayerCount> colors;
};
static_assert(sizeof(TerrainSkirtUniforms) == 16 * (TerrainSkirtLayerCount + 1));

// Four outward-facing walls. One quad per boundary tile, no interior walls or
// bottom cap. The shader supplies all 40 bands without extra layer geometry.
TerrainSkirtMesh BuildTerrainSkirt(const TerrainHeightmap& terrain, const TerrainSkirtSettings& settings = {});
TerrainSkirtUniforms MakeTerrainSkirtUniforms(const TerrainSkirtSettings& settings = {});
