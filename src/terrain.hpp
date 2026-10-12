#pragma once
#include <cstdint>
#include <vector>

constexpr std::uint32_t MapSize = 256;
constexpr float MapExtent = static_cast<float>(MapSize);

// Terrain Vertex struct. Y is elevation. 1 unit = 1 tile length.
struct Vertex { float x, y, z; };
static_assert(sizeof(Vertex) == 12);

struct TerrainMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Vertex> lines;
};

inline TerrainMesh BuildTerrain()
{
    TerrainMesh mesh;
    constexpr auto stride = MapSize + 1;

    mesh.vertices.reserve(stride * stride);
    mesh.indices.reserve(MapSize * MapSize * 6);

    for (std::uint32_t z = 0; z <= MapSize; ++z)
    {
        for (std::uint32_t x = 0; x <= MapSize; ++x)
        {
            mesh.vertices.push_back({static_cast<float>(x), 0.0f, static_cast<float>(z)});
        }
    }

    for (std::uint32_t z = 0; z < MapSize; ++z) 
    {
        for (std::uint32_t x = 0; x < MapSize; ++x) 
        {
            const auto a = z * stride + x;
            const auto b = a + 1;
            const auto c = a + stride;
            const auto d = c + 1;

            // Two tris per square, both with an upward normal.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    /**
     * Draw square black wireframe around each tile.
     */
    mesh.lines.reserve(4 * stride);

    for (std::uint32_t i = 0; i <= MapSize; ++i) 
    {
        const float p = static_cast<float>(i);
        mesh.lines.push_back({p, 0.0f, 0.0f});
        mesh.lines.push_back({p, 0.0f, MapExtent});
        mesh.lines.push_back({0.0f, 0.0f, p});
        mesh.lines.push_back({MapExtent, 0.0f, p});
    }

    return mesh;
}
