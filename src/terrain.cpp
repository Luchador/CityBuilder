#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    // Deterministic lattice values: unsigned overflow is intentional.
    float LatticeValue(std::uint32_t x, std::uint32_t z, std::uint32_t seed)
    {
        std::uint32_t hash = seed ^ (x * 0x9e3779b9u) ^ (z * 0x85ebca6bu);
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;

        return static_cast<float>(hash >> 8) / 16777215.0f;
    }

    float Fade(float t)
    {
        // Quintic interpolation has zero first/second derivatives at cell edges.
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float ValueNoise(float x, float z, std::uint32_t seed)
    {
        // Generator coordinates are nonnegative and bounded by validated settings.
        const auto ix = static_cast<std::uint32_t>(std::floor(x));
        const auto iz = static_cast<std::uint32_t>(std::floor(z));
        const float tx = Fade(x - static_cast<float>(ix));
        const float tz = Fade(z - static_cast<float>(iz));
        const float top = std::lerp(LatticeValue(ix, iz, seed), LatticeValue(ix + 1, iz, seed), tx);
        const float bottom = std::lerp(LatticeValue(ix, iz + 1, seed), LatticeValue(ix + 1, iz + 1, seed), tx);

        return std::lerp(top, bottom, tz);
    }

    float HillNoise(float x, float z, const TerrainSettings& settings)
    {
        float total = 0, weight = 1, weights = 0, frequency = 1;

        for (std::uint32_t octave = 0; octave < 3; ++octave)
        {
            // Smaller layers add variation without turning the surface into spikes.
            const float offset = static_cast<float>(octave);
            total += weight * ValueNoise(x / settings.hillSize * frequency + 13.37f * offset, z / settings.hillSize * frequency + 7.11f * offset, settings.seed + octave * 0x9e3779b9u);
            weights += weight;
            frequency *= 2;
            weight *= 0.5f;
        }

        return total / weights;
    }

    void Validate(const TerrainSettings& settings)
    {
        if (!std::isfinite(settings.hillHeight) || settings.hillHeight < 0 || settings.hillHeight > TerrainMaxElevation ||
            !std::isfinite(settings.hillSize) || settings.hillSize < 8 || settings.hillSize > 1024 ||
            !std::isfinite(settings.flatCoverage) || settings.flatCoverage < 0 || settings.flatCoverage > 1)
        {
            throw std::invalid_argument("Terrain settings require hillHeight 0..256, hillSize 8..1024, and flatCoverage 0..1");
        }
    }

    void ValidatePalette(const TerrainPalette& palette)
    {
        const auto validChannel = [](float value)
        {
            return std::isfinite(value) && value >= 0 && value <= 1;
        };
        for (std::size_t i = 0; i < palette.bands.size(); ++i)
        {
            const auto& band = palette.bands[i];
    
            if (!std::isfinite(band.upperElevation) || !std::isfinite(band.transitionWidth) || band.transitionWidth < 0 || !validChannel(band.color.r) || !validChannel(band.color.g) || !validChannel(band.color.b))
            {
                throw std::invalid_argument("Terrain palette requires finite elevations, nonnegative transition widths, and RGB values in 0..1");
            }

            if (i > 0 && (band.upperElevation <= palette.bands[i - 1].upperElevation || band.transitionWidth > band.upperElevation - palette.bands[i - 1].upperElevation))
            {
                throw std::invalid_argument("Terrain palette boundaries must increase and transition zones must fit within their bands");
            }
        }
        if (palette.bands.front().upperElevation != TerrainSeaLevel ||
            palette.bands.back().upperElevation != TerrainMaxElevation ||
            palette.bands.back().transitionWidth != 0)
        {
            throw std::invalid_argument("Terrain palette must end water at 0 and snow at 256, with no transition after snow");
        }
    }

    TerrainColor SamplePalette(float height, const TerrainPalette& palette)
    {
        for (std::size_t i = 0; i + 1 < palette.bands.size(); ++i)
        {
            const auto& band = palette.bands[i];
    
            if (height >= band.upperElevation) 
            {
                continue;
            }

            const float blendStart = band.upperElevation - band.transitionWidth;

            if (band.transitionWidth == 0 || height <= blendStart) 
            {
                return band.color;
            }

            const float t = std::clamp((height - blendStart) / band.transitionWidth, 0.0f, 1.0f);
            const float blend = t * t * (3.0f - 2.0f * t);
            const auto& next = palette.bands[i + 1].color;
    
            return {std::lerp(band.color.r, next.r, blend), std::lerp(band.color.g, next.g, blend), std::lerp(band.color.b, next.b, blend)};
        }
        return palette.bands.back().color;
    }
}

TerrainColor SampleTerrainColor(float height, const TerrainPalette& palette)
{
    if (!std::isfinite(height)) 
    {
        throw std::invalid_argument("Terrain color requires a finite elevation");
    }

    ValidatePalette(palette);

    return SamplePalette(height, palette);
}

TerrainHeightmap GenerateTerrain(const TerrainSettings& settings)
{
    Validate(settings);
    TerrainHeightmap terrain;

    if (settings.hillHeight == 0 || settings.flatCoverage == 1) 
    {
        return terrain;
    }

    for (std::uint32_t z = 0; z <= MapSize; ++z)
    {
        for (std::uint32_t x = 0; x <= MapSize; ++x)
        {
            terrain.heights[z * TerrainStride + x] = HillNoise(static_cast<float>(x), static_cast<float>(z), settings);
        }
    }

    // Pick a noise threshold by coverage, so each seed has substantial level land.
    // Coherent noise makes these vertices form areas instead of isolated flat tiles.
    auto sorted = terrain.heights;
    const auto cutoffIndex = static_cast<std::size_t>(settings.flatCoverage * static_cast<float>(TerrainVertexCount - 1));

    std::nth_element(sorted.begin(), sorted.begin() + cutoffIndex, sorted.end());
    const float cutoff = sorted[cutoffIndex];
    const float peak = *std::max_element(terrain.heights.begin(), terrain.heights.end());
    const float range = peak - cutoff;

    if (range <= 0)
    {
        std::fill(terrain.heights.begin(), terrain.heights.end(), 0.0f);

        return terrain;
    }

    // Use integer step counts, then convert back to world units. Clamp the step
    // count so a non-multiple hillHeight (e.g. 8.15) still respects the height cap.
    constexpr float stepsPerUnit = 1.0f / TerrainHeightStep;
    float maximumStep = std::floor(settings.hillHeight * stepsPerUnit);

    if (maximumStep / stepsPerUnit > settings.hillHeight)
    {
        --maximumStep; // Handle a floating-point value immediately below a step.
    }

    for (float& height : terrain.heights)
    {
        const float t = std::clamp((height - cutoff) / range, 0.0f, 1.0f);

        /**
         * Shape the hills first then snap the shared corners to increments of 0.2.
         */
        const float smoothHeight = settings.hillHeight * (t * t * (3.0f - 2.0f * t));
        const float step = std::min(std::round(smoothHeight * stepsPerUnit), maximumStep);

        height = step / stepsPerUnit;
    }

    return terrain;
}

TerrainMesh BuildTerrain(const TerrainHeightmap& terrain, const TerrainPalette& palette)
{
    if (terrain.heights.size() != TerrainVertexCount || !std::all_of(terrain.heights.begin(), terrain.heights.end(), [](float h) { return std::isfinite(h) && h <= TerrainMaxElevation; }))
    {
        throw std::invalid_argument("Terrain heightmap must contain 257x257 finite heights at or below 256");
    }
    ValidatePalette(palette);

    TerrainMesh mesh;
    mesh.vertices.reserve(TerrainVertexCount);
    mesh.indices.reserve(MapSize * MapSize * 6);

    for (std::uint32_t z = 0; z <= MapSize; ++z)
    {
        for (std::uint32_t x = 0; x <= MapSize; ++x)
        {
            const auto left = x == 0 ? x : x - 1;
            const auto right = std::min(x + 1, MapSize);
            const auto back = z == 0 ? z : z - 1;
            const auto front = std::min(z + 1, MapSize);
    
            // Central differences inside, one-sided differences at map boundaries.
            const float dx = (terrain.Height(right, z) - terrain.Height(left, z)) / static_cast<float>(right - left);
            const float dz = (terrain.Height(x, front) - terrain.Height(x, back)) / static_cast<float>(front - back);
            const float inverseLength = 1.0f / std::sqrt(dx * dx + 1.0f + dz * dz);
            const float height = terrain.Height(x, z);
            const TerrainColor color = SamplePalette(height, palette);
            mesh.vertices.push_back({static_cast<float>(x), height, static_cast<float>(z),
                                     -dx * inverseLength, inverseLength, -dz * inverseLength,
                                     color.r, color.g, color.b});
        }
    }

    for (std::uint32_t z = 0; z < MapSize; ++z)
    {
        for (std::uint32_t x = 0; x < MapSize; ++x)
        {
            const auto a = z * TerrainStride + x;
            const auto b = a + 1;
            const auto c = a + TerrainStride;
            const auto d = c + 1;
            // Two triangles per tile, both with an upward-facing winding.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    // One segment per tile edge, shared between neighboring tiles. The old long
    // map-spanning lines cannot follow hills. Reuse the exact terrain endpoints.
    mesh.lines.reserve(4 * MapSize * TerrainStride);
    for (std::uint32_t z = 0; z <= MapSize; ++z)
    {
        for (std::uint32_t x = 0; x < MapSize; ++x)
        {
            const auto a = z * TerrainStride + x;
            mesh.lines.push_back(mesh.vertices[a]);
            mesh.lines.push_back(mesh.vertices[a + 1]);
        }
    }

    for (std::uint32_t x = 0; x <= MapSize; ++x)
    {
        for (std::uint32_t z = 0; z < MapSize; ++z)
        {
            const auto a = z * TerrainStride + x;
            mesh.lines.push_back(mesh.vertices[a]);
            mesh.lines.push_back(mesh.vertices[a + TerrainStride]);
        }
    }

    return mesh;
}
