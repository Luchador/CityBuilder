#pragma once

#include "occupancy.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <bit>
#include <vector>

// World-space directions; independent of camera rotation. One quarter-turn
// maps (x,z) to (-z,x), so West -> North -> East -> South.
enum RoadConnection : std::uint8_t
{
    RoadWest = 1, RoadNorth = 2, RoadEast = 4, RoadSouth = 8,
    RoadNorthWest = 16, RoadNorthEast = 32, RoadSouthEast = 64, RoadSouthWest = 128
};

inline constexpr std::uint8_t RoadDiagonalNWSE = RoadNorthWest | RoadSouthEast;
inline constexpr std::uint8_t RoadDiagonalNESW = RoadNorthEast | RoadSouthWest;
inline constexpr std::uint8_t RoadAllDiagonals = RoadDiagonalNWSE | RoadDiagonalNESW;
inline constexpr float RoadDiagonalLength = 1.41421356237f;

// Axis of a straight or end. Junctions use both axes.
constexpr std::uint8_t DiagonalRoadAxis(std::uint8_t connections)
{
    if (connections && !(connections & ~RoadDiagonalNWSE)) return RoadDiagonalNWSE;
    if (connections && !(connections & ~RoadDiagonalNESW)) return RoadDiagonalNESW;
    return 0;
}

constexpr bool SupportedDiagonalConnections(std::uint8_t connections)
{
    return !(connections & 15) && (connections == 0 || DiagonalRoadAxis(connections) ||
        std::popcount(connections) >= 3);
}

enum class RoadShape : std::size_t { Straight, ThreeWay, FourWay, DeadEnd, Curve, DiagonalThreeWay, DiagonalFourWay, Count };
struct RoadVisual
{
    RoadShape shape = RoadShape::DeadEnd;
    float quarterTurns = 0; // Half a quarter-turn is a 45-degree diagonal.
    float lengthScale = 1;
};

constexpr std::uint8_t RotateRoadConnections(std::uint8_t mask, unsigned turns)
{
    for (unsigned i = 0; i < turns % 4; ++i)
    {
        const unsigned low = mask & 15, high = mask >> 4;
        mask = static_cast<std::uint8_t>((((low << 1) | (low >> 3)) & 15) |
            ((((high << 1) | (high >> 3)) & 15) << 4));
    }
    return mask;
}

struct RoadAsset
{
    const char* model;
    const char* texture;
    std::uint8_t connections; // Authored openings AFTER the GLB node transforms.
    float uScale = 1.0f, uOffset = 0.0f;
    bool extendDiagonalApproaches = false;
};

inline constexpr char StraightRoadModelPath[] = "models/transportation/roads/road_straight.glb";
inline constexpr char StraightRoadTexturePath[] = "models/transportation/roads/road_straight_basecolor.png";
inline constexpr std::array<RoadAsset, static_cast<std::size_t>(RoadShape::Count)> RoadAssets{{
    {StraightRoadModelPath, StraightRoadTexturePath, RoadWest | RoadEast},
    {"models/transportation/roads/road_3way.glb", "models/transportation/roads/road_3way_basecolor.png", RoadWest | RoadNorth | RoadEast},
    {"models/transportation/roads/road_4way.glb", "models/transportation/roads/road_4way_basecolor.png", 15},
    {"models/transportation/roads/road_end.glb", "models/transportation/roads/road_end_basecolor.png", RoadWest},
    // Curve UVs span the whole strip. Sample the same U=.25..75 road-width
    // portion of the straight texture, with its centerline bent by the UVs.
    {"models/transportation/roads/road_curve.glb", StraightRoadTexturePath, RoadWest | RoadSouth, 0.5f, 0.25f},
    // Derive longer approaches from the existing junctions; their centers and
    // widths stay unchanged. Connections here include the final 45-degree yaw.
    {"models/transportation/roads/road_3way.glb", "models/transportation/roads/road_3way_basecolor.png",
        RoadNorthWest | RoadNorthEast | RoadSouthEast, 1, 0, true},
    {"models/transportation/roads/road_4way.glb", "models/transportation/roads/road_4way_basecolor.png",
        RoadAllDiagonals, 1, 0, true}
}};

constexpr RoadVisual GetRoadVisual(std::uint8_t connections)
{
    if (const auto axis = DiagonalRoadAxis(connections))
    {
        if (connections == axis)
            return {RoadShape::Straight, axis == RoadDiagonalNWSE ? 0.5f : 1.5f, RoadDiagonalLength};
        const float turns = connections == RoadNorthWest ? 0.5f : connections == RoadNorthEast ? 1.5f :
            connections == RoadSouthEast ? 2.5f : 3.5f;
        return {RoadShape::DeadEnd, turns, RoadDiagonalLength};
    }
    for (std::size_t shape = 0; shape < RoadAssets.size(); ++shape)
    {
        for (unsigned turns = 0; turns < 4; ++turns)
        {
            if (RotateRoadConnections(RoadAssets[shape].connections, turns) == connections)
            {
                return {static_cast<RoadShape>(shape), static_cast<float>(turns) +
                    (RoadAssets[shape].extendDiagonalApproaches ? .5f : 0.0f)};
            }
        }
    }

    // A lone tile has no connections yet. Use an end until a neighbor is added.
    return {RoadShape::DeadEnd, 0};
}

// Plane under a road, in world axes and relative to the tile center. Valid
// slopes have exactly one nonzero gradient; road-width edge pairs stay level.
struct RoadSurface
{
    float elevation = 0.0f;
    float slopeX = 0.0f, slopeZ = 0.0f;

    constexpr std::uint8_t AllowedConnections() const
    {
        return slopeX != 0 ? RoadWest | RoadEast : slopeZ != 0 ? RoadNorth | RoadSouth : 15;
    }
    constexpr float HeightAt(float localX, float localZ) const
    {
        return elevation + slopeX * localX + slopeZ * localZ;
    }
};

// Persistent CPU data only. The renderer shares one mesh per shape.
struct Road
{
    InfrastructureId id = NoInfrastructure;
    int tileX = 0;
    int tileZ = 0;
    RoadSurface surface;
    std::uint8_t connections = 0;
    // Allowed corridor axes, including both axes at a junction. 0 keeps the
    // cardinal rules. A pair of adjacent diagonal ports is not a supported bend.
    std::uint8_t diagonalAxis = 0;
};

constexpr RoadVisual GetRoadVisual(const Road& road)
{
    if (road.connections) return GetRoadVisual(road.connections);
    if (road.diagonalAxis)
        return GetRoadVisual(road.diagonalAxis == RoadDiagonalNWSE ? RoadNorthWest : RoadNorthEast);
    // An isolated end must still align with its slope. Aim its opening downhill.
    const auto& s = road.surface;
    if (s.slopeX != 0) return GetRoadVisual(s.slopeX > 0 ? RoadWest : RoadEast);
    if (s.slopeZ != 0) return GetRoadVisual(s.slopeZ > 0 ? RoadNorth : RoadSouth);
    return GetRoadVisual(0);
}

enum class RoadPlacementStatus { Allowed, ExistingRoad, OutsideMap, NotFlat, Underwater, Occupied, WrongSlopeDirection, UnsupportedConnection, UpgradeRoad };

constexpr bool RoadPlacementAccepted(RoadPlacementStatus status)
{
    return status == RoadPlacementStatus::Allowed || status == RoadPlacementStatus::ExistingRoad ||
        status == RoadPlacementStatus::UpgradeRoad;
}

// An orthogonal route with at most one bend, or one straight diagonal.
// World validates endpoints and shape before traversal; never traverse
// arbitrary/unvalidated coordinates.
struct RoadPath
{
    int startX = 0, startZ = 0, endX = 0, endZ = 0;
    bool xFirst = true;
    bool diagonal = false;
    bool operator==(const RoadPath&) const = default;

    // Near 45 degrees, snap to a straight diagonal inside the drag rectangle.
    // Else preserve the existing L route. Equal 2:1 drags remain orthogonal.
    static RoadPath FromDrag(int x, int z, int targetX, int targetZ)
    {
        const int dx = std::abs(targetX - x), dz = std::abs(targetZ - z);
        RoadPath result{x, z, targetX, targetZ, dx >= dz};
        if (std::min(dx, dz) * 2 > std::max(dx, dz))
        {
            const int steps = std::min(dx, dz);
            result.endX = x + (targetX > x ? steps : -steps);
            result.endZ = z + (targetZ > z ? steps : -steps);
            result.diagonal = true;
        }
        return result;
    }

    bool HasValidShape() const
    {
        // World checks bounds before calling this (subtraction cannot overflow).
        return !diagonal || (startX != endX && std::abs(endX - startX) == std::abs(endZ - startZ));
    }

    bool Contains(int x, int z) const
    {
        if (x < std::min(startX, endX) || x > std::max(startX, endX) ||
            z < std::min(startZ, endZ) || z > std::max(startZ, endZ)) return false;
        if (diagonal) return std::abs(x - startX) == std::abs(z - startZ);
        return xFirst ? (z == startZ || x == endX) : (x == startX || z == endZ);
    }

    // Required directions along the proposed route, before skipping obstacles.
    // A bend or a cross-slope drag cannot masquerade as disconnected end tiles.
    std::uint8_t ConnectionsAt(int x, int z) const
    {
        if (!Contains(x, z)) return 0;
        if (diagonal)
            return static_cast<std::uint8_t>((Contains(x - 1, z - 1) ? RoadNorthWest : 0) |
                (Contains(x + 1, z - 1) ? RoadNorthEast : 0) | (Contains(x + 1, z + 1) ? RoadSouthEast : 0) |
                (Contains(x - 1, z + 1) ? RoadSouthWest : 0));
        return static_cast<std::uint8_t>((Contains(x - 1, z) ? RoadWest : 0) |
            (Contains(x, z - 1) ? RoadNorth : 0) | (Contains(x + 1, z) ? RoadEast : 0) |
            (Contains(x, z + 1) ? RoadSouth : 0));
    }

    template<class Visitor> void ForEachTile(Visitor visit) const
    {
        int x = startX, z = startZ;

        visit(x, z);
        if (diagonal)
        {
            while (x != endX)
            {
                x += endX > x ? 1 : -1;
                z += endZ > z ? 1 : -1;
                visit(x, z);
            }
            return;
        }
        const auto moveX = [&] { while (x != endX) { x += endX > x ? 1 : -1; visit(x, z); } };
        const auto moveZ = [&] { while (z != endZ) { z += endZ > z ? 1 : -1; visit(x, z); } };

        if (xFirst) 
        { 
            moveX(); 
            moveZ(); 
        }
        else 
        { 
            moveZ(); 
            moveX(); 
        }
    }
};

struct RoadEvaluation
{
    bool valid = false;
    std::uint32_t changes = 0, unchanged = 0, blocked = 0;
    std::uint32_t upgrades = 0; // Included in changes; do not allocate a new ID.
};

struct PlannedRoadTile
{
    int x = 0, z = 0;
    RoadPlacementStatus status = RoadPlacementStatus::OutsideMap;
    std::uint8_t diagonalAxis = 0;
};

// One plan drives evaluation, preview and commit. In particular, a junction
// must use the neighbors that can actually be built after obstacles are skipped.
struct RoadPlan
{
    RoadEvaluation evaluation;
    std::vector<PlannedRoadTile> tiles;
};

struct RoadPlacementResult
{
    RoadEvaluation evaluation;
    std::uint32_t changed = 0;
};
