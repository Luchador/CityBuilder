#include "road_mesh.hpp"
#include "map_dimensions.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

StaticMesh LoadRoadMesh(const std::filesystem::path& path, const RoadAsset& asset)
{
    auto geometry = LoadStaticGlb(path, 1.0f / MetersPerTile);
    for (auto& v : geometry.vertices)
    {
        if (std::abs(v.x) > .5001f || std::abs(v.z) > .5001f)
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
