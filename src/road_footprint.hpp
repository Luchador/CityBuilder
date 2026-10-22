#pragma once

#include "road.hpp"

#include <array>
#include <limits>
#include <utility>

// W/N/E/S bits indicate the side tiles reserved in addition to the anchor.
// Ends reserve room for their eventual straight. Transitions only spill toward
// their diagonal opening, leaving their cardinal approach tile available.
constexpr std::uint8_t RoadFootprintSides(std::uint8_t axis, std::uint8_t transition = 0)
{
    if (!transition) return axis ? 15 : 0;
    const auto diagonal = transition & 240;
    return static_cast<std::uint8_t>(
        ((diagonal & (RoadNorthWest | RoadSouthWest)) ? RoadWest : RoadEast) |
        ((diagonal & (RoadNorthWest | RoadNorthEast)) ? RoadNorth : RoadSouth));
}

constexpr bool RoadReservesTile(const Road& road, int x, int z)
{
    const int dx = x - road.tileX, dz = z - road.tileZ;
    const auto sides = RoadFootprintSides(road.diagonalAxis, road.transitionConnections);
    return (!dx && !dz) || (dx == -1 && !dz && (sides & RoadWest)) ||
        (!dx && dz == -1 && (sides & RoadNorth)) || (dx == 1 && !dz && (sides & RoadEast)) ||
        (!dx && dz == 1 && (sides & RoadSouth));
}

// Conservative convex cells contain the complete road meshes. Integer
// half-tile coordinates make exact edge contact safe without an epsilon.
// Sharing a reserved terrain tile is legal only when these cells do not overlap.
struct RoadCell
{
    struct Point { int x, z; };
    std::array<Point, 5> points{};
    int count = 4;

    explicit RoadCell(const Road& road)
    {
        if (road.transitionConnections)
        {
            count = 5;
            points = {{{-2, 0}, {-1, -1}, {1, -1}, {1, 1}, {0, 2}}};
            unsigned turns = 0;
            bool mirrored = false;
            for (; turns < 4; ++turns)
            {
                if (RotateRoadConnections(RoadEast | RoadSouthWest, turns) == road.transitionConnections) break;
                if (RotateRoadConnections(RoadEast | RoadNorthWest, turns) == road.transitionConnections)
                { mirrored = true; break; }
            }
            for (auto& point : points)
            {
                if (mirrored) point.z = -point.z;
                for (unsigned turn = 0; turn < turns; ++turn)
                { const int x = point.x; point.x = -point.z; point.z = x; }
            }
        }
        else if (road.diagonalAxis) points = {{{-2, 0}, {0, -2}, {2, 0}, {0, 2}, {}}};
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
