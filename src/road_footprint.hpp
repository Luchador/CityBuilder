#pragma once

#include "road.hpp"
#include "road_collision_shapes.hpp"

#include <cmath>

#include <array>
#include <limits>
#include <utility>

// W/N/E/S bits indicate the side tiles reserved in addition to the anchor.
// Ends reserve room for their eventual straight. Transitions only spill toward
// their diagonal opening, leaving their cardinal approach tile available.
constexpr std::uint8_t RoadFootprintSides(std::uint8_t axis, std::uint8_t mixed = 0)
{
    if (!mixed) return axis ? 15 : 0;
    return static_cast<std::uint8_t>(
        ((mixed & (RoadNorthWest | RoadSouthWest)) ? RoadWest : 0) |
        ((mixed & (RoadNorthWest | RoadNorthEast)) ? RoadNorth : 0) |
        ((mixed & (RoadNorthEast | RoadSouthEast)) ? RoadEast : 0) |
        ((mixed & (RoadSouthWest | RoadSouthEast)) ? RoadSouth : 0));
}

constexpr bool RoadReservesTile(const Road& road, int x, int z)
{
    const int dx = x - road.tileX, dz = z - road.tileZ;
    const auto sides = RoadFootprintSides(road.diagonalAxis, road.mixedConnections);
    return (!dx && !dz) || (dx == -1 && !dz && (sides & RoadWest)) ||
        (!dx && dz == -1 && (sides & RoadNorth)) || (dx == 1 && !dz && (sides & RoadEast)) ||
        (!dx && dz == 1 && (sides & RoadSouth));
}

// Conservative convex cells contain the complete road meshes. Integer
// half-tile coordinates make exact edge contact safe without an epsilon.
// This is only the broad phase. Mixed approaches can have overlapping cells
// while their actual pavement fits without any overlap.
struct RoadCell
{
    struct Point { int x, z; };
    std::array<Point, 5> points{};
    int count = 4;

    explicit RoadCell(const Road& road)
    {
        if (IsRoadTransition(road.mixedConnections))
        {
            count = 5;
            points = {{{-2, 0}, {-1, -1}, {1, -1}, {1, 1}, {0, 2}}};
            unsigned turns = 0;
            bool mirrored = false;
            for (; turns < 4; ++turns)
            {
                if (RotateRoadConnections(RoadEast | RoadSouthWest, turns) == road.mixedConnections) break;
                if (RotateRoadConnections(RoadEast | RoadNorthWest, turns) == road.mixedConnections)
                { mirrored = true; break; }
            }
            for (auto& point : points)
            {
                if (mirrored) point.z = -point.z;
                for (unsigned turn = 0; turn < turns; ++turn)
                { const int x = point.x; point.x = -point.z; point.z = x; }
            }
        }
        else if (road.diagonalAxis || road.mixedConnections) points = {{{-2, 0}, {0, -2}, {2, 0}, {0, 2}, {}}};
        else points = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {}}};
        for (int i = 0; i < count; ++i)
        { points[i].x += 2 * road.tileX; points[i].z += 2 * road.tileZ; }
    }
};

inline bool RoadCellsOverlap(const Road& a, const Road& b)
{
    const RoadCell first(a), second(b);
    const auto separated = [&](const RoadCell& edges)
    {
        for (int i = 0; i < edges.count; ++i)
        {
            const auto p = edges.points[i], q = edges.points[(i + 1) % edges.count];
            const int nx = p.z - q.z, nz = q.x - p.x;
            const auto interval = [&](const RoadCell& cell)
            {
                int low = std::numeric_limits<int>::max(), high = std::numeric_limits<int>::min();
                for (int j = 0; j < cell.count; ++j)
                {
                    const int projection = cell.points[j].x * nx + cell.points[j].z * nz;
                    low = std::min(low, projection); high = std::max(high, projection);
                }
                return std::pair{low, high};
            };
            const auto [al, ah] = interval(first); const auto [bl, bh] = interval(second);
            if (ah <= bl || bh <= al) return true;
        }
        return false;
    };
    return !separated(first) && !separated(second);
}

// Narrow phase uses the authored pavement triangles, including the flared end
// caps and curved transitions. Local coordinates keep precision on large maps.
// Touching seams are allowed; crossing pavement between tile centers is not.
inline bool RoadMeshesOverlap(const Road& a, const Road& b)
{
    if (!RoadCellsOverlap(a, b)) return false;
    static_assert(RoadCollisionShapes.size() == RoadAssets.size());
    const auto transform = [](const Road& road, int offsetX, int offsetZ)
    {
        struct Geometry
        {
            std::array<RoadCollisionPoint, RoadCollisionMaxPoints> points{};
            std::size_t count = 0;
        } result;
        const auto visual = GetRoadVisual(road);
        const double angle = visual.quarterTurns * 1.5707963267948966;
        const double cosine = std::cos(angle), sine = std::sin(angle);
        for (const auto p : RoadCollisionShapes[static_cast<std::size_t>(visual.shape)])
            result.points[result.count++] = {offsetX + p.x * visual.lengthScale * cosine - p.z * sine,
                                            offsetZ + p.x * visual.lengthScale * sine + p.z * cosine};
        return result;
    };
    const auto first = transform(a, 0, 0), second = transform(b, b.tileX - a.tileX, b.tileZ - a.tileZ);
    const auto separated = [](const RoadCollisionPoint* edges, const RoadCollisionPoint* other)
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto p = edges[i], q = edges[(i + 1) % 3];
            const double nx = p.z - q.z, nz = q.x - p.x;
            const auto interval = [&](const RoadCollisionPoint* triangle)
            {
                double low = std::numeric_limits<double>::max(), high = -low;
                for (int j = 0; j < 3; ++j)
                {
                    const double projection = triangle[j].x * nx + triangle[j].z * nz;
                    low = std::min(low, projection); high = std::max(high, projection);
                }
                return std::pair{low, high};
            };
            const auto [al, ah] = interval(edges); const auto [bl, bh] = interval(other);
            const double tolerance = 0.000001 * (std::abs(nx) + std::abs(nz));
            if (ah <= bl + tolerance || bh <= al + tolerance) return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < first.count; i += 3)
        for (std::size_t j = 0; j < second.count; j += 3)
            if (!separated(first.points.data() + i, second.points.data() + j) &&
                !separated(second.points.data() + j, first.points.data() + i)) return true;
    return false;
}
