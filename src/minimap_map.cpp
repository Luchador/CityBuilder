#include "minimap_map.hpp"
#include "water.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr std::uint32_t PixelsPerTile = 2;
    constexpr std::uint32_t SamplesPerAxis = 2;

    bool ValidColor(TerrainColor color)
    {
        return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
               color.r >= 0 && color.r <= 1 && color.g >= 0 && color.g <= 1 && color.b >= 0 && color.b <= 1;
    }

    TerrainColor SampleTile(const TerrainMesh& terrain, std::uint32_t x, std::uint32_t z,
                            float u, float v, const MinimapTileOverlay& overlay)
    {
        // Subdivision preserves the original ground triangles. Test water using
        // their corner heights, exactly as BuildWater does. This also avoids
        // classifying an exact sea-level point as wet due to inset rounding.
        const auto corner = (z * TerrainMeshStride + x) * TerrainSubdivisions;
        const float ha = terrain.vertices[corner].y;
        const float hb = terrain.vertices[corner + TerrainSubdivisions].y;
        const float hc = terrain.vertices[corner + TerrainSubdivisions * TerrainMeshStride].y;
        const float hd = terrain.vertices[corner + TerrainSubdivisions * (TerrainMeshStride + 1)].y;
        const float height = u + v <= 1.0f
            ? (1.0f - u - v) * ha + u * hb + v * hc
            : (u + v - 1.0f) * hd + (1.0f - v) * hb + (1.0f - u) * hc;
        if (IsWaterElevation(height)) return WaterColor;

        // Find the narrow border or large center interval, then interpolate the
        // same subtriangle colors the GPU draws. u/v are logical tile fractions.
        const auto interval = [](float t) -> std::uint32_t
        {
            return t < TerrainTileOffsets[1] ? 0u : t < TerrainTileOffsets[2] ? 1u : 2u;
        };
        const auto sx = x * TerrainSubdivisions + interval(u);
        const auto sz = z * TerrainSubdivisions + interval(v);
        const auto a = sz * TerrainMeshStride + sx;
        const auto b = a + 1;
        const auto c = a + TerrainMeshStride;
        const auto d = c + 1;
        u = (static_cast<float>(x) + u - terrain.vertices[a].x) / (terrain.vertices[b].x - terrain.vertices[a].x);
        v = (static_cast<float>(z) + v - terrain.vertices[a].z) / (terrain.vertices[c].z - terrain.vertices[a].z);

        // Match BuildTerrain: {a,c,b}, {b,c,d}, not bilinear interpolation.
        const bool upper = u + v <= 1.0f;
        const Vertex& p = terrain.vertices[upper ? a : d];
        const Vertex& q = terrain.vertices[b];
        const Vertex& r = terrain.vertices[c];
        const float wp = upper ? 1.0f - u - v : u + v - 1.0f;
        const float wq = upper ? u : 1.0f - v;
        const float wr = upper ? v : 1.0f - u;

        const TerrainColor ground{wp * p.r + wq * q.r + wr * r.r,
                                  wp * p.g + wq * q.g + wr * r.g,
                                  wp * p.b + wq * q.b + wr * r.b};
        return {std::lerp(ground.r, overlay.color.r, overlay.opacity),
                std::lerp(ground.g, overlay.color.g, overlay.opacity),
                std::lerp(ground.b, overlay.color.b, overlay.opacity)};
    }

    std::uint8_t ToByte(float value)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }
}

MinimapImage BuildMinimapImage(const TerrainMesh& terrain, std::span<const MinimapTileOverlay> overlays)
{
    if (terrain.vertices.size() != TerrainMeshVertexCount)
    {
        throw std::invalid_argument("Minimap requires the subdivided grid returned by BuildTerrain");
    }
    for (std::uint32_t z = 0; z <= TerrainMeshSize; ++z)
    {
        for (std::uint32_t x = 0; x <= TerrainMeshSize; ++x)
        {
            const auto& vertex = terrain.vertices[z * TerrainMeshStride + x];
            if (vertex.x != TerrainMeshCoordinate(x) || vertex.z != TerrainMeshCoordinate(z) ||
                !std::isfinite(vertex.y) || !ValidColor({vertex.r, vertex.g, vertex.b}))
            {
                throw std::invalid_argument("Minimap requires valid terrain positions and colors");
            }
        }
    }
    if (!overlays.empty() && overlays.size() != MapSize * MapSize)
    {
        throw std::invalid_argument("Minimap overlay must contain one entry per tile");
    }
    for (const auto& overlay : overlays)
    {
        if (!ValidColor(overlay.color) || !std::isfinite(overlay.opacity) || overlay.opacity < 0 || overlay.opacity > 1)
        {
            throw std::invalid_argument("Minimap overlay colors and opacity must be in 0..1");
        }
    }

    MinimapImage image;
    image.width = image.height = MapSize * PixelsPerTile;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    constexpr float sampleWeight = 1.0f / (SamplesPerAxis * SamplesPerAxis);

    for (std::uint32_t py = 0; py < image.height; ++py)
    {
        for (std::uint32_t px = 0; px < image.width; ++px)
        {
            const auto x = px / PixelsPerTile;
            const auto z = py / PixelsPerTile;
            const MinimapTileOverlay overlay = overlays.empty() ? MinimapTileOverlay{} : overlays[z * MapSize + x];
            TerrainColor color{0, 0, 0};

            // Four subpixel samples soften the shoreline without flooding an
            // entire tile just because one of its corners is underwater.
            for (std::uint32_t sy = 0; sy < SamplesPerAxis; ++sy)
            {
                for (std::uint32_t sx = 0; sx < SamplesPerAxis; ++sx)
                {
                    const float u = (static_cast<float>(px % PixelsPerTile) + (sx + 0.5f) / SamplesPerAxis) / PixelsPerTile;
                    const float v = (static_cast<float>(py % PixelsPerTile) + (sy + 0.5f) / SamplesPerAxis) / PixelsPerTile;
                    const auto sample = SampleTile(terrain, x, z, u, v, overlay);
                    color.r += sample.r * sampleWeight;
                    color.g += sample.g * sampleWeight;
                    color.b += sample.b * sampleWeight;
                }
            }

            const auto offset = (static_cast<std::size_t>(py) * image.width + px) * 4;
            image.rgba[offset] = ToByte(color.r);
            image.rgba[offset + 1] = ToByte(color.g);
            image.rgba[offset + 2] = ToByte(color.b);
            image.rgba[offset + 3] = 255;
        }
    }
    return image;
}
