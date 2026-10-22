#include "road_mesh.hpp"
#include "map_dimensions.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

StaticMesh LoadRoadMesh(const std::filesystem::path& path, const RoadAsset& asset)
{
    auto geometry = LoadStaticGlb(path, 1.0f / MetersPerTile);
    if (asset.fitGridTransition)
    {
        // Both supplied exports start at world X=-8 m. Move their bend tile
        // center (-16,0) to the origin after baking the GLB node transforms.
        // The final straight tail was authored against an unstretched diagonal.
        // Clip that tail to x +/- z = -1, the shared diagonal-cell boundary.
        const float side = asset.connections & RoadSouthWest ? 1.0f : -1.0f;
        for (auto& v : geometry.vertices) v.x += 1.0f;
        const auto source = geometry.vertices;
        for (auto& v : geometry.vertices)
        {
            const float along = -v.x + side * v.z;
            if (along > 1.0f)
            {
                // Preserve the texture density on the shortened tail, using
                // the previous vertex on the same side of the authored strip.
                const StaticMeshVertex* previous = nullptr;
                float previousAlong = -1.0f;
                for (const auto& p : source)
                {
                    const float pa = -p.x + side * p.z;
                    if (std::abs(p.u - v.u) < .00001f && pa <= 1.0f && pa > previousAlong)
                    { previous = &p; previousAlong = pa; }
                }
                if (!previous) throw std::runtime_error(std::string(asset.model) + " has no transition-tail UV anchor");
                v.v = std::lerp(previous->v, v.v, (1.0f - previousAlong) / (along - previousAlong));
                const float trim = (along - 1.0f) * .5f;
                v.x += trim;
                v.z -= side * trim;
            }
            if (std::abs(v.x) + std::abs(v.z) > 1.0001f || v.x > .5001f || side * v.z < -.5001f)
                throw std::runtime_error(std::string(asset.model) + " does not fit the grid-transition footprint");
        }
    }
    for (auto& v : geometry.vertices)
    {
        if (!asset.fitGridTransition && (std::abs(v.x) > .5001f || std::abs(v.z) > .5001f))
            throw std::runtime_error(std::string(asset.model) + " must fit a centered 16 x 16 meter source tile");
        v.u = v.u * asset.uScale + asset.uOffset;
        if (asset.extendDiagonalApproaches)
        {
            // The center is an 8x8 m square. Extend only the 4 m approaches to
            // reach 11.314 m from the origin, without widening the road/center.
            constexpr float extension = (RoadDiagonalLength * .5f - .25f) / .25f;
            const auto extend = [&](float& coordinate, float& normal)
            {
                if (std::abs(coordinate) > .25001f)
                {
                    coordinate = std::copysign(.25f + (std::abs(coordinate) - .25f) * extension, coordinate);
                    normal /= extension;
                }
            };
            extend(v.x, v.nx);
            extend(v.z, v.nz);
            const float length = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
            if (length > 0) { v.nx /= length; v.ny /= length; v.nz /= length; }
        }
    }
    return geometry;
}
