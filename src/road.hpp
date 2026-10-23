#pragma once

#include "occupancy.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <bit>
#include <span>
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

// Axis of a straight or end. Curves and junctions use both axes.
constexpr std::uint8_t DiagonalRoadAxis(std::uint8_t connections)
{
    if (connections && !(connections & ~RoadDiagonalNWSE)) return RoadDiagonalNWSE;
    if (connections && !(connections & ~RoadDiagonalNESW)) return RoadDiagonalNESW;
    return 0;
}

constexpr bool SupportedDiagonalConnections(std::uint8_t connections)
{
    // Every subset of the four diagonal ports has a mesh: end, straight,
    // 90-degree curve, three-way junction, or four-way junction.
    return !(connections & 15);
}

constexpr std::uint8_t DiagonalRoadAxes(std::uint8_t connections)
{
    if (connections & 15) return 0;
    return static_cast<std::uint8_t>(((connections & RoadDiagonalNWSE) ? RoadDiagonalNWSE : 0) |
                                     ((connections & RoadDiagonalNESW) ? RoadDiagonalNESW : 0));
}

constexpr bool IsDiagonalRoadCurve(std::uint8_t connections)
{
    return SupportedDiagonalConnections(connections) && std::popcount(connections) == 2 &&
        !DiagonalRoadAxis(connections);
}

enum class RoadShape : std::size_t
{
    Straight, ThreeWay, FourWay, DeadEnd, Curve,
    DiagonalThreeWay, DiagonalFourWay, GridToDiagonalLeft, GridToDiagonalRight,
    MixedThreeWayGridNE, MixedThreeWayGridNW, MixedThreeWayDiagonalE, MixedThreeWayDiagonalN,
    MixedFourWayNESW, MixedFourWayNWSE, DiagonalCurve,
    DiagonalCurveAcuteIntersection, DiagonalCurveReflexIntersection, DiagonalCurveDoubleIntersection, Count
};
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

// Only a gentle 45-degree turn: the outward ports are 135 degrees apart.
constexpr bool IsRoadTransition(std::uint8_t connections)
{
    for (unsigned turn = 0; turn < 4; ++turn)
        if (connections == RotateRoadConnections(RoadEast | RoadSouthWest, turn) ||
            connections == RotateRoadConnections(RoadEast | RoadNorthWest, turn)) return true;
    return false;
}

// Additional logical road cells drawn by the anchor's single model. Offsets
// and allowed ports are authored in the same orientation as asset.connections.
// Adjacent cardinal and diagonal cells are supported. Shoulder reservations
// remain separate from model ownership (diagonal pavement crosses tile edges).
struct RoadModuleCell
{
    int x, z;
    std::uint8_t ports;
};
inline constexpr std::array<RoadModuleCell, 1> InsideStraightCell{{{1, 0, RoadWest | RoadEast}}};
inline constexpr std::array<RoadModuleCell, 2> GridNwSlipCells{{
    {-1, 0, RoadWest | RoadEast}, {-1, -1, RoadDiagonalNWSE}}};
inline constexpr std::array<RoadModuleCell, 2> GridNeSlipCells{{
    {1, 0, RoadWest | RoadEast}, {1, -1, RoadDiagonalNESW}}};
inline constexpr std::array<RoadModuleCell, 2> DiagonalNSlipCells{{
    {0, -1, RoadNorth | RoadSouth}, {-1, -1, RoadDiagonalNWSE}}};
inline constexpr std::array<RoadModuleCell, 2> DiagonalESlipCells{{
    {1, 0, RoadWest | RoadEast}, {1, 1, RoadDiagonalNWSE}}};
inline constexpr std::array<RoadModuleCell, 4> NwSeSlipCells{{
    {-1, 0, RoadWest | RoadEast}, {-1, -1, RoadDiagonalNWSE},
    {1, 0, RoadWest | RoadEast}, {1, 1, RoadDiagonalNWSE}}};
inline constexpr std::array<RoadModuleCell, 4> NeSwSlipCells{{
    {1, 0, RoadWest | RoadEast}, {1, -1, RoadDiagonalNESW},
    {-1, 0, RoadWest | RoadEast}, {-1, 1, RoadDiagonalNESW}}};
inline constexpr int RoadFootprintRadius = 2;


struct RoadAsset
{
    const char* model;
    const char* texture;
    std::uint8_t connections; // Openings after source fitting and base rotation.
    float uScale = 1.0f, uOffset = 0.0f;
    bool extendDiagonalApproaches = false;
    bool fitGridTransition = false;
    bool mixedJunction = false; // Authored diagonally, with optional UVs/texture.
    float sourceHalfExtent = 0.5f; // Tile units, after GLB node transforms.
    float rotationOffset = 0.0f; // Quarter-turns, added to the instance rotation.
    std::span<const RoadModuleCell> moduleCells{};
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
        RoadAllDiagonals, 1, 0, true},
    {"models/transportation/roads/road_grid_to_diagonal_curve_left.glb", StraightRoadTexturePath,
        RoadEast | RoadSouthWest, 1, 0, false, true},
    {"models/transportation/roads/road_grid_to_diagonal_curve_right.glb", StraightRoadTexturePath,
        RoadEast | RoadNorthWest, 1, 0, false, true},
    {"models/transportation/roads/road_mixed_3way_grid_ne.glb", "models/transportation/roads/road_mixed_3way_grid_ne_basecolor.png",
        RoadWest | RoadEast | RoadNorthEast, 1, 0, false, false, true, .5f, 0, GridNeSlipCells},
    {"models/transportation/roads/road_mixed_3way_grid_nw.glb", "models/transportation/roads/road_mixed_3way_grid_nw_basecolor.png",
        RoadWest | RoadEast | RoadNorthWest, 1, 0, false, false, true, .5f, 0, GridNwSlipCells},
    {"models/transportation/roads/road_mixed_3way_diagonal_e.glb", "models/transportation/roads/road_mixed_3way_diagonal_e_basecolor.png",
        RoadNorthWest | RoadSouthEast | RoadEast, 1, 0, false, false, true, .5f, 0, DiagonalESlipCells},
    {"models/transportation/roads/road_mixed_3way_diagonal_n.glb", "models/transportation/roads/road_mixed_3way_diagonal_n_basecolor.png",
        RoadNorthWest | RoadSouthEast | RoadNorth, 1, 0, false, false, true, .5f, 0, DiagonalNSlipCells},
    {"models/transportation/roads/road_mixed_4way_ne_sw.glb", "models/transportation/roads/road_mixed_4way_ne_sw_basecolor.png",
        RoadWest | RoadEast | RoadNorthEast | RoadSouthWest, 1, 0, false, false, true, .5f, 0, NeSwSlipCells},
    {"models/transportation/roads/road_mixed_4way_nw_se.glb", "models/transportation/roads/road_mixed_4way_nw_se_basecolor.png",
        RoadWest | RoadEast | RoadNorthWest | RoadSouthEast, 1, 0, false, false, true, .5f, 0, NwSeSlipCells},
    // Authored with full-length E/N openings. Rotate 45 degrees to SE/NE;
    // preserve the wider radius and UVs, without stretching the geometry.
    {"models/transportation/roads/road_diagonal_curve.glb", StraightRoadTexturePath,
        RoadNorthEast | RoadSouthEast, 0.5f, 0.25f, false, false, false, RoadDiagonalLength * 0.5f, 0.5f},
    // The fitted corner junctions already face NE/SE. The acute/double models
    // own the inside straight cell, ending at X=24 m. The reflex
    // junction stays one module cell. No extra diagonal rotation/stretch.
    // Optional dedicated textures use the existing plain-asphalt fallback.
    {"models/transportation/roads/road_diagonal_curve_acute_intersection.glb",
        "models/transportation/roads/road_diagonal_curve_acute_intersection_basecolor.png",
        RoadNorthEast | RoadSouthEast | RoadEast, 1, 0, false, false, true, .5f, 0, InsideStraightCell},
    {"models/transportation/roads/road_diagonal_curve_reflex_intersection.glb",
        "models/transportation/roads/road_diagonal_curve_reflex_intersection_basecolor.png",
        RoadNorthEast | RoadSouthEast | RoadWest, 1, 0, false, false, true},
    {"models/transportation/roads/road_diagonal_curve_double_intersection.glb",
        "models/transportation/roads/road_diagonal_curve_double_intersection_basecolor.png",
        RoadNorthEast | RoadSouthEast | RoadWest | RoadEast, 1, 0, false, false, true, .5f, 0, InsideStraightCell}
}};

template<class Visitor> constexpr void ForEachRoadModuleCell(std::uint8_t mask, Visitor visit)
{
    if (!mask) return;
    for (const auto& asset : RoadAssets) if (!asset.moduleCells.empty())
        for (unsigned turn = 0; turn < 4; ++turn)
            if (RotateRoadConnections(asset.connections, turn) == mask)
            {
                for (auto cell : asset.moduleCells)
                {
                    for (unsigned i = 0; i < turn; ++i) { const int x = cell.x; cell.x = -cell.z; cell.z = x; }
                    cell.ports = RotateRoadConnections(cell.ports, turn);
                    visit(cell);
                }
                return;
            }
}

constexpr std::uint8_t RoadModulePortsAt(std::uint8_t mask, int dx, int dz)
{
    std::uint8_t ports = 0;
    ForEachRoadModuleCell(mask, [&](auto cell) { if (cell.x == dx && cell.z == dz) ports = cell.ports; });
    return ports;
}

// Owned cells must be adjacent, straight and aligned with their root opening.
// A diagonal cell plus its shoulders needs the two-tile reservation radius.
static_assert([]
{
    for (const auto& asset : RoadAssets) for (auto cell : asset.moduleCells)
        if (std::max(cell.x < 0 ? -cell.x : cell.x, cell.z < 0 ? -cell.z : cell.z) != 1 ||
            (cell.ports != (RoadWest | RoadEast) && cell.ports != (RoadNorth | RoadSouth) &&
             cell.ports != RoadDiagonalNWSE && cell.ports != RoadDiagonalNESW) || asset.rotationOffset != 0 || !asset.mixedJunction)
            return false;
    return true;
}());

constexpr bool IsMixedRoadJunction(std::uint8_t connections)
{
    for (const auto& asset : RoadAssets)
        if (asset.mixedJunction)
            for (unsigned turn = 0; turn < 4; ++turn)
                if (RotateRoadConnections(asset.connections, turn) == connections) return true;
    return false;
}

constexpr bool SupportedMixedConnections(std::uint8_t connections)
{
    return IsRoadTransition(connections) || IsMixedRoadJunction(connections);
}

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
                    (RoadAssets[shape].extendDiagonalApproaches ? .5f : 0.0f) + RoadAssets[shape].rotationOffset};
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
    // Allowed corridor axes, including both axes at a curve or junction.
    // Zero keeps the cardinal rules.
    std::uint8_t diagonalAxis = 0;
    // Exact ports of a grid/diagonal bend or junction. Zero uses the axis rules.
    // The planner upgrades this mask only when a matching mesh is available.
    std::uint8_t mixedConnections = 0;
    // Derived module membership: this logical cell still participates in the
    // road graph/occupancy, but its geometry belongs to the neighboring anchor.
    // Zero ports means this road owns its own model.
    std::uint8_t modulePorts = 0;
    int modelOwnerOffsetX = 0, modelOwnerOffsetZ = 0;
};

constexpr bool HasRoadModel(const Road& road) { return road.modulePorts == 0; }

constexpr std::uint8_t RoadPorts(const Road& road)
{
    return road.modulePorts ? road.modulePorts : road.mixedConnections ? road.mixedConnections :
        road.diagonalAxis ? road.diagonalAxis : road.surface.AllowedConnections();
}

constexpr RoadVisual GetRoadVisual(const Road& road)
{
    if (road.mixedConnections) return GetRoadVisual(road.mixedConnections);
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

    // Constant-time lookup into ForEachTile order, including both legs of an L.
    std::size_t IndexOf(int x, int z) const
    {
        if (diagonal) return static_cast<std::size_t>(std::abs(x - startX));
        return static_cast<std::size_t>(xFirst ?
            (z == startZ ? std::abs(x - startX) : std::abs(endX - startX) + std::abs(z - startZ)) :
            (x == startX ? std::abs(z - startZ) : std::abs(endZ - startZ) + std::abs(x - startX)));
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
    std::uint8_t mixedConnections = 0;
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
