#include "terrain_skirt.hpp"

#include <cmath>
#include <stdexcept>

namespace
{
    void Validate(const TerrainSkirtSettings& settings)
    {
        if (!std::isfinite(settings.bottomY) || settings.bottomY < -512 || settings.bottomY > TerrainMaxElevation ||
            !std::isfinite(settings.thicknessExponent) || settings.thicknessExponent < 1 || settings.thicknessExponent > 4)
            throw std::invalid_argument("Terrain skirt requires bottomY -512..256 and thicknessExponent 1..4");
        const auto channel = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
        for (const auto& color : settings.colors)
            if (!channel(color.r) || !channel(color.g) || !channel(color.b))
                throw std::invalid_argument("Terrain skirt colors require finite RGB values in 0..1");
    }
}

TerrainSkirtMesh BuildTerrainSkirt(const TerrainHeightmap& terrain, const TerrainSkirtSettings& settings)
{
    terrain.Validate();
    Validate(settings);

    TerrainSkirtMesh mesh;
    mesh.vertices.reserve(4 * TerrainStride * 2);
    mesh.indices.reserve(4 * MapSize * 6);

    // A continuous walk around the perimeter. Each wall keeps its own corner
    // normals for a sharp 90-degree edge, but corner positions match exactly.
    for (std::uint32_t side = 0; side < 4; ++side)
    {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const float nx[] = {0, 1, 0, -1};
        const float nz[] = {-1, 0, 1, 0};
        for (std::uint32_t i = 0; i <= MapSize; ++i)
        {
            const std::uint32_t x = side == 0 ? i : side == 1 ? MapSize : side == 2 ? MapSize - i : 0;
            const std::uint32_t z = side == 0 ? 0 : side == 1 ? i : side == 2 ? MapSize : MapSize - i;
            const float top = terrain.Height(x, z);
            if (top - settings.bottomY < TerrainHeightStep)
                throw std::invalid_argument("Terrain skirt bottomY must be at least one height step below every terrain edge; lower bottomY");

            const float fx = static_cast<float>(x), fz = static_cast<float>(z);
            mesh.vertices.push_back({fx, top, fz, nx[side], 0, nz[side], top});
            mesh.vertices.push_back({fx, settings.bottomY, fz, nx[side], 0, nz[side], top});
        }
        for (std::uint32_t i = 0; i < MapSize; ++i)
        {
            const auto a = first + i * 2; // top-left, bottom-left, top-right, bottom-right
            // Cross products point outward on all four sides. The renderer
            // explicitly uses counterclockwise front faces and backface culling.
            mesh.indices.insert(mesh.indices.end(), {a, a + 2, a + 1, a + 2, a + 3, a + 1});
        }
    }
    return mesh;
}

TerrainSkirtUniforms MakeTerrainSkirtUniforms(const TerrainSkirtSettings& settings)
{
    Validate(settings);
    TerrainSkirtUniforms uniforms{};
    uniforms.settings = {settings.bottomY, 1.0f / settings.thicknessExponent,
                         static_cast<float>(TerrainSkirtLayerCount), 0};
    for (std::size_t i = 0; i < settings.colors.size(); ++i)
    {
        const auto& color = settings.colors[i];
        uniforms.colors[i] = {color.r, color.g, color.b, 1};
    }
    return uniforms;
}
