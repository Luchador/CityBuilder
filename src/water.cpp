#include "water.hpp"

#include <limits>

WaterMesh BuildWater(const TerrainHeightmap& terrain)
{
    terrain.Validate();
    const auto tileCount = terrain.dimensions.TileCount();
    const auto stride = terrain.dimensions.CornerStride();
    WaterMesh mesh;
    constexpr auto missing = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> cornerIndices(terrain.dimensions.CornerCount(), missing);

    const auto corner = [&](std::uint32_t x, std::uint32_t z)
    {
        auto& index = cornerIndices[z * stride + x];
        if (index == missing)
        {
            index = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({static_cast<float>(x), TerrainSeaLevel, static_cast<float>(z),
                                     TerrainSeaLevel - terrain.Height(x, z)});
        }
        return index;
    };

    for (std::uint32_t z = 0; z < tileCount; ++z)
    {
        for (std::uint32_t x = 0; x < tileCount; ++x)
        {
            if (!IsWaterElevation(terrain.Height(x, z)) &&
                !IsWaterElevation(terrain.Height(x + 1, z)) &&
                !IsWaterElevation(terrain.Height(x, z + 1)) &&
                !IsWaterElevation(terrain.Height(x + 1, z + 1))) continue;

            const auto a = corner(x, z);
            const auto b = corner(x + 1, z);
            const auto c = corner(x, z + 1);
            const auto d = corner(x + 1, z + 1);
            // Match the terrain's diagonal so interpolated water depth follows
            // the same two ground triangles, including partially submerged tiles.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}
