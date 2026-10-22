#pragma once

#include "occupancy.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

// World-space directions; independent of camera rotation. One quarter-turn
// maps (x,z) to (-z,x), so West -> North -> East -> South.
enum RoadConnection : std::uint8_t
{
    RoadWest = 1, RoadNorth = 2, RoadEast = 4, RoadSouth = 8
};

enum class RoadShape : std::size_t { Straight, ThreeWay, FourWay, DeadEnd, Curve, Count };
struct RoadVisual
{
    RoadShape shape = RoadShape::DeadEnd;
    std::uint8_t quarterTurns = 0;
};

constexpr std::uint8_t RotateRoadConnections(std::uint8_t mask, unsigned turns)
{
    for (unsigned i = 0; i < turns % 4; ++i)
        mask = static_cast<std::uint8_t>(((mask << 1) | (mask >> 3)) & 15);
    return mask;
}

struct RoadAsset
{
    const char* model;
    const char* texture;
    std::uint8_t connections; // Authored openings AFTER the GLB node transforms.
    float uScale = 1.0f, uOffset = 0.0f;
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
    {"models/transportation/roads/road_curve.glb", StraightRoadTexturePath, RoadWest | RoadSouth, 0.5f, 0.25f}
}};

constexpr RoadVisual GetRoadVisual(std::uint8_t connections)
{
    for (std::size_t shape = 0; shape < RoadAssets.size(); ++shape)
    {
        for (unsigned turns = 0; turns < 4; ++turns)
        {
            if (RotateRoadConnections(RoadAssets[shape].connections, turns) == connections)
            {
                return {static_cast<RoadShape>(shape), static_cast<std::uint8_t>(turns)};
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
};

constexpr RoadVisual GetRoadVisual(const Road& road)
{
    if (road.connections) return GetRoadVisual(road.connections);
    // An isolated end must still align with its slope. Aim its opening downhill.
    const auto& s = road.surface;
    if (s.slopeX != 0) return GetRoadVisual(s.slopeX > 0 ? RoadWest : RoadEast);
    if (s.slopeZ != 0) return GetRoadVisual(s.slopeZ > 0 ? RoadNorth : RoadSouth);
    return GetRoadVisual(0);
}

enum class RoadPlacementStatus { Allowed, ExistingRoad, OutsideMap, NotFlat, Underwater, Occupied, WrongSlopeDirection };

// A one-tile-wide orthogonal route with at most one bend. World validates the
// endpoints before traversal; never traverse arbitrary/unvalidated coordinates.
struct RoadPath
{
    int startX = 0, startZ = 0, endX = 0, endZ = 0;
    bool xFirst = true;
    bool operator==(const RoadPath&) const = default;

    bool Contains(int x, int z) const
    {
        if (x < std::min(startX, endX) || x > std::max(startX, endX) ||
            z < std::min(startZ, endZ) || z > std::max(startZ, endZ)) return false;
        return xFirst ? (z == startZ || x == endX) : (x == startX || z == endZ);
    }

    // Required directions along the proposed route, before skipping obstacles.
    // A bend or a cross-slope drag cannot masquerade as disconnected end tiles.
    std::uint8_t ConnectionsAt(int x, int z) const
    {
        if (!Contains(x, z)) return 0;
        return static_cast<std::uint8_t>((Contains(x - 1, z) ? RoadWest : 0) |
            (Contains(x, z - 1) ? RoadNorth : 0) | (Contains(x + 1, z) ? RoadEast : 0) |
            (Contains(x, z + 1) ? RoadSouth : 0));
    }

    template<class Visitor> void ForEachTile(Visitor visit) const
    {
        int x = startX, z = startZ;

        visit(x, z);
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
};

struct RoadPlacementResult
{
    RoadEvaluation evaluation;
    std::uint32_t changed = 0;
};
