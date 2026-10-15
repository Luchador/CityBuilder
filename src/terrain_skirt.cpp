#include "terrain_skirt.hpp"

#include <cmath>
#include <stdexcept>

namespace
{
    double LayerRandom(std::uint32_t layer, std::uint32_t seed)
    {
        // Integer hashing keeps the layout deterministic without depending on
        // a standard library's random distribution implementation.
        std::uint32_t hash = seed ^ ((layer + 1) * 0x9e3779b9u);
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;

        return static_cast<double>(hash >> 8) / 16777215.0;
    }

    void Validate(const TerrainSkirtSettings& settings)
    {
        if (!std::isfinite(settings.bottomY) || settings.bottomY < -512 || settings.bottomY > TerrainMaxElevation || !std::isfinite(settings.thicknessExponent) || settings.thicknessExponent < 1 || settings.thicknessExponent > 4)
        {
            throw std::invalid_argument("Terrain skirt requires bottomY -512..256 and thicknessExponent 1..4");
        }

        if (!std::isfinite(settings.thicknessVariation) || settings.thicknessVariation < 0 || settings.thicknessVariation > 0.95f)
        {
            throw std::invalid_argument("Terrain skirt thicknessVariation must be in 0..0.95");
        }

        const auto channel = [](float value) 
        { 
            return std::isfinite(value) && value >= 0 && value <= 1; 
        };
    
        for (const auto& color : settings.colors)
        {
            if (!channel(color.r) || !channel(color.g) || !channel(color.b))
            {
                throw std::invalid_argument("Terrain skirt colors require finite RGB values in 0..1");
            }
        }
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
            {
                throw std::invalid_argument("Terrain skirt bottomY must be at least one height step below every terrain edge; lower bottomY");
            }

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

TerrainSkirtUniforms MakeTerrainSkirtUniforms(const TerrainSkirtSettings& settings, std::uint32_t seed)
{
    Validate(settings);
    TerrainSkirtUniforms uniforms{};

    uniforms.settings = {settings.bottomY, 0, static_cast<float>(TerrainSkirtLayerCount), 0};

    // First find the regular, exponent-shaped thickness of each layer. Vary
    // those thicknesses individually, then normalize so the full stack still
    // spans precisely from the terrain surface to the fixed bottom.
    std::array<double, TerrainSkirtLayerCount> widths{};
    double previousDepth = 0, totalWidth = 0;
    for (std::uint32_t i = 0; i < TerrainSkirtLayerCount; ++i)
    {
        const double depth = std::pow(static_cast<double>(i + 1) / TerrainSkirtLayerCount,
                                      static_cast<double>(settings.thicknessExponent));
        const double multiplier = 1.0 + settings.thicknessVariation * (2.0 * LayerRandom(i, seed) - 1.0);
        widths[i] = (depth - previousDepth) * multiplier;
        totalWidth += widths[i];
        previousDepth = depth;
    }

    double cumulativeWidth = 0;

    for (std::size_t i = 0; i < settings.colors.size(); ++i)
    {
        const auto& color = settings.colors[i];
        cumulativeWidth += widths[i];
        const float bottomDepth = i + 1 == TerrainSkirtLayerCount ? 1.0f : static_cast<float>(cumulativeWidth / totalWidth);
        uniforms.colors[i] = {color.r, color.g, color.b, bottomDepth};
    }

    return uniforms;
}
