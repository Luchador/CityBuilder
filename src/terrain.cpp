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
        const auto& lakes = settings.lakes;
        if (lakes.count > 64 ||
            !std::isfinite(lakes.minRadius) || lakes.minRadius < 4 || lakes.minRadius > 64 ||
            !std::isfinite(lakes.maxRadius) || lakes.maxRadius < lakes.minRadius || lakes.maxRadius > 64 ||
            !std::isfinite(lakes.depth) || lakes.depth < 0 || lakes.depth > TerrainMaxElevation)
        {
            throw std::invalid_argument("Lake settings require count 0..64, radii 4..64 with minRadius <= maxRadius, and depth 0..256");
        }
    }

    void AddLakes(TerrainHeightmap& terrain, const TerrainSettings& settings)
    {
        const auto& lakes = settings.lakes;
        const auto tileCount = terrain.dimensions.TileCount();
        const auto stride = terrain.dimensions.CornerStride();
        float maximumDepthStep = std::floor(lakes.depth / TerrainHeightStep);
        if (maximumDepthStep * TerrainHeightStep > lakes.depth) --maximumDepthStep;
        if (lakes.count == 0 || maximumDepthStep < 1) return;

        // Carve through the dry bank as well as below the water. Lake depth
        // remains measured from sea level, independently of the raised plains.
        const float bankHeight = TerrainBaseElevation - TerrainSeaLevel;
        const float basinDrop = bankHeight + lakes.depth;
        const float maximumDropSteps = bankHeight / TerrainHeightStep + maximumDepthStep;

        // Distance to a hill or map edge in king moves (all eight neighbors cost
        // one). This UNDERestimates Euclidean distance, giving each lake a safe
        // footprint entirely inside the first pass's flat, raised land.
        std::vector<int> clearance(terrain.dimensions.CornerCount());
        for (std::uint32_t z = 0; z <= tileCount; ++z)
        {
            for (std::uint32_t x = 0; x <= tileCount; ++x)
            {
                const auto index = z * stride + x;
                clearance[index] = terrain.heights[index] == TerrainBaseElevation
                    ? static_cast<int>(std::min({x, z, tileCount - x, tileCount - z})) : 0;
            }
        }
        for (std::uint32_t z = 1; z < tileCount; ++z)
        {
            for (std::uint32_t x = 1; x < tileCount; ++x)
            {
                const auto i = z * stride + x;
                clearance[i] = std::min(clearance[i], 1 + std::min({clearance[i - 1],
                    clearance[i - stride - 1], clearance[i - stride], clearance[i - stride + 1]}));
            }
        }
        for (std::uint32_t z = tileCount - 1; z > 0; --z)
        {
            for (std::uint32_t x = tileCount - 1; x > 0; --x)
            {
                const auto i = z * stride + x;
                clearance[i] = std::min(clearance[i], 1 + std::min({clearance[i + 1],
                    clearance[i + stride - 1], clearance[i + stride], clearance[i + stride + 1]}));
            }
        }

        struct Candidate { std::uint32_t x, z; float rank; };
        struct Lake { float x, z, radius; };
        std::vector<Candidate> candidates;
        const std::uint32_t lakeSeed = settings.seed ^ 0x6c616b65u;
        for (std::uint32_t z = 1; z < tileCount; ++z)
        {
            for (std::uint32_t x = 1; x < tileCount; ++x)
            {
                if (static_cast<float>(clearance[z * stride + x]) >= lakes.minRadius + 2.0f)
                    candidates.push_back({x, z, LatticeValue(x, z, lakeSeed)});
            }
        }
        // Hash ranks make the order repeatable without depending on the standard
        // library's random distribution/shuffle implementation. Break ties by position.
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.rank != b.rank) return a.rank < b.rank;
            if (a.z != b.z) return a.z < b.z;
            return a.x < b.x;
        });

        std::vector<Lake> placed;
        for (const auto& candidate : candidates)
        {
            if (placed.size() >= lakes.count) break;
            const float cx = static_cast<float>(candidate.x);
            const float cz = static_cast<float>(candidate.z);
            const float available = static_cast<float>(clearance[candidate.z * stride + candidate.x]) - 2.0f;
            const float radius = std::lerp(lakes.minRadius, std::min(lakes.maxRadius, available),
                                          LatticeValue(candidate.x, candidate.z, lakeSeed ^ 0x92d68ca2u));
            const bool overlaps = std::any_of(placed.begin(), placed.end(), [&](const Lake& other)
            {
                const float dx = cx - other.x, dz = cz - other.z;
                const float separation = radius + other.radius + 4.0f;
                return dx * dx + dz * dz < separation * separation;
            });
            if (overlaps) continue;

            const float angle = 6.28318530718f * LatticeValue(candidate.x, candidate.z, lakeSeed ^ 0xa535a9b7u);
            const float cosine = std::cos(angle), sine = std::sin(angle);
            const float aspect = 0.60f + 0.40f * LatticeValue(candidate.x, candidate.z, lakeSeed ^ 0x7f4a7c15u);

            const int startX = static_cast<int>(std::floor(cx - radius));
            const int endX = static_cast<int>(std::ceil(cx + radius));
            const int startZ = static_cast<int>(std::floor(cz - radius));
            const int endZ = static_cast<int>(std::ceil(cz + radius));
            for (int z = startZ; z <= endZ; ++z)
            {
                for (int x = startX; x <= endX; ++x)
                {
                    const float fx = static_cast<float>(x), fz = static_cast<float>(z);
                    const float dx = fx - cx, dz = fz - cz;
                    const float along = dx * cosine + dz * sine;
                    const float across = (-dx * sine + dz * cosine) / aspect;
                    const float distance = std::sqrt(along * along + across * across);
                    // Coherent variation rounds the shore without a perfect circle.
                    // The varying radius never exceeds the safe footprint above.
                    const float shoreRadius = radius * (0.65f + 0.35f * ValueNoise(fx / 7.0f, fz / 7.0f, lakeSeed));
                    const float t = std::clamp(1.0f - distance / shoreRadius, 0.0f, 1.0f);
                    const float smoothDrop = basinDrop * t * t * (3.0f - 2.0f * t);
                    const float steps = std::min(std::round(smoothDrop / TerrainHeightStep), maximumDropSteps);
                    const auto index = static_cast<std::size_t>(z) * stride + static_cast<std::size_t>(x);
                    if (terrain.heights[index] == TerrainBaseElevation && steps > 0)
                        terrain.heights[index] = TerrainBaseElevation - steps * TerrainHeightStep;
                }
            }
            placed.push_back({cx, cz, radius});
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

namespace
{
TerrainHeightmap GenerateHills(const TerrainSettings& settings)
{
    TerrainHeightmap terrain(settings.dimensions);
    const auto tileCount = terrain.dimensions.TileCount();
    const auto stride = terrain.dimensions.CornerStride();
    const float hillHeight = std::min(settings.hillHeight, TerrainMaxElevation - TerrainBaseElevation);

    if (hillHeight == 0 || settings.flatCoverage == 1)
    {
        std::fill(terrain.heights.begin(), terrain.heights.end(), TerrainBaseElevation);
        return terrain;
    }

    for (std::uint32_t z = 0; z <= tileCount; ++z)
    {
        for (std::uint32_t x = 0; x <= tileCount; ++x)
        {
            terrain.heights[z * stride + x] = HillNoise(static_cast<float>(x), static_cast<float>(z), settings);
        }
    }

    // Pick a noise threshold by coverage, so each seed has substantial level land.
    // Coherent noise makes these vertices form areas instead of isolated flat tiles.
    auto sorted = terrain.heights;
    const auto cutoffIndex = static_cast<std::size_t>(settings.flatCoverage * static_cast<float>(terrain.dimensions.CornerCount() - 1));

    std::nth_element(sorted.begin(), sorted.begin() + cutoffIndex, sorted.end());
    const float cutoff = sorted[cutoffIndex];
    const float peak = *std::max_element(terrain.heights.begin(), terrain.heights.end());
    const float range = peak - cutoff;

    if (range <= 0)
    {
        std::fill(terrain.heights.begin(), terrain.heights.end(), TerrainBaseElevation);

        return terrain;
    }

    // Use integer step counts, then convert back to world units. Clamp the step
    // count so a non-multiple hillHeight (e.g. 8.15) still respects the height cap.
    constexpr float stepsPerUnit = 1.0f / TerrainHeightStep;
    float maximumStep = std::floor(hillHeight * stepsPerUnit);

    if (maximumStep / stepsPerUnit > hillHeight)
    {
        --maximumStep; // Handle a floating-point value immediately below a step.
    }

    for (float& height : terrain.heights)
    {
        const float t = std::clamp((height - cutoff) / range, 0.0f, 1.0f);

        // Shape the hills first, then snap the shared corners to TerrainHeightStep.
        const float smoothHeight = hillHeight * (t * t * (3.0f - 2.0f * t));
        const float step = std::min(std::round(smoothHeight * stepsPerUnit), maximumStep);

        height = TerrainBaseElevation + step / stepsPerUnit;
    }

    return terrain;
}
}

TerrainHeightmap GenerateTerrain(const TerrainSettings& settings)
{
    Validate(settings);
    auto terrain = GenerateHills(settings);
    AddLakes(terrain, settings); // Runs even when the first pass produced an entirely flat map.
    return terrain;
}

TerrainMesh BuildTerrain(const TerrainHeightmap& terrain, const TerrainPalette& palette)
{
    terrain.Validate();
    ValidatePalette(palette);

    const auto tileCount = terrain.dimensions.TileCount();
    const auto stride = terrain.dimensions.CornerStride();
    TerrainMesh mesh;
    mesh.dimensions = terrain.dimensions;
    mesh.vertices.reserve(terrain.dimensions.CornerCount());
    mesh.indices.reserve(tileCount * tileCount * 6);

    for (std::uint32_t z = 0; z <= tileCount; ++z)
    {
        for (std::uint32_t x = 0; x <= tileCount; ++x)
        {
            const auto left = x == 0 ? x : x - 1;
            const auto right = std::min(x + 1, tileCount);
            const auto back = z == 0 ? z : z - 1;
            const auto front = std::min(z + 1, tileCount);
    
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

    for (std::uint32_t z = 0; z < tileCount; ++z)
    {
        for (std::uint32_t x = 0; x < tileCount; ++x)
        {
            const auto a = z * stride + x;
            const auto b = a + 1;
            const auto c = a + stride;
            const auto d = c + 1;
            // Two triangles per tile, both with an upward-facing winding.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    // One segment per tile edge, shared between neighboring tiles. The old long
    // map-spanning lines cannot follow hills. Share the terrain vertex buffer
    // instead of duplicating a complete Vertex for every line endpoint.
    mesh.lineIndices.reserve(4 * tileCount * stride);
    for (std::uint32_t z = 0; z <= tileCount; ++z)
    {
        for (std::uint32_t x = 0; x < tileCount; ++x)
        {
            const auto a = z * stride + x;
            mesh.lineIndices.push_back(a);
            mesh.lineIndices.push_back(a + 1);
        }
    }

    for (std::uint32_t x = 0; x <= tileCount; ++x)
    {
        for (std::uint32_t z = 0; z < tileCount; ++z)
        {
            const auto a = z * stride + x;
            mesh.lineIndices.push_back(a);
            mesh.lineIndices.push_back(a + stride);
        }
    }

    return mesh;
}
