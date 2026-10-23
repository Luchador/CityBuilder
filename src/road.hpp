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
    DiagonalCurveAcuteIntersection, DiagonalCurveReflexIntersection, DiagonalCurveDoubleIntersection,
    MixedFourWayArrow, GridCurveInnerThreeWay, GridCurveOuterThreeWay, Count
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
inline constexpr std::array<RoadModuleCell, 3> ArrowSlipCells{{
    {-1, 0, RoadWest | RoadEast}, {0, -1, RoadNorth | RoadSouth}, {-1, -1, RoadDiagonalNWSE}}};
inline constexpr std::array<RoadModuleCell, 2> GridCornerCells{{
    {-1, 0, RoadWest | RoadEast}, {0, -1, RoadNorth | RoadSouth}}};
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
        RoadNorthEast | RoadSouthEast | RoadWest | RoadEast, 1, 0, false, false, true, .5f, 0, InsideStraightCell},
    // Diagonal through route with two perpendicular grid approaches on its
    // NW side. The fitted mesh includes both turning lanes and all three
    // adjoining straight cells; its source's 45-degree turn is already baked.
    {"models/transportation/roads/road_mixed_4way_arrow.glb",
        "models/transportation/roads/road_mixed_4way_arrow_basecolor.png",
        RoadNorthWest | RoadSouthEast | RoadWest | RoadNorth, 1, 0, false, false, true, .5f, 0, ArrowSlipCells},
    // A W/N grid corner with a diagonal approaching from inside (NW) or
    // outside (SE). Both include the cardinal approach cells. The inner
    // model also includes NW, matching the four-way arrow's ownership.
    {"models/transportation/roads/road_grid_curve_inner_3way.glb",
        "models/transportation/roads/road_grid_curve_inner_3way_basecolor.png",
        RoadWest | RoadNorth | RoadNorthWest, 1, 0, false, false, true, .5f, 0, ArrowSlipCells},
    {"models/transportation/roads/road_grid_curve_outer_3way.glb",
        "models/transportation/roads/road_grid_curve_outer_3way_basecolor.png",
        RoadWest | RoadNorth | RoadSouthEast, 1, 0, false, false, true, .5f, 0, GridCornerCells}
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
    // Cardinal edges deliberately left closed when a parallel road was built.
    // A later drag across the shared edge can connect the two roads explicitly.
    std::uint8_t suppressedConnections = 0;
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

enum class RoadPlacementStatus
{
    Allowed, ExistingRoad, OutsideMap, NotFlat, Underwater, Occupied,
    WrongSlopeDirection, UnsupportedConnection, UpgradeRoad,
    DisconnectedSegment // Otherwise usable, but beyond the first valid drag section.
};

constexpr bool RoadPlacementAccepted(RoadPlacementStatus status)
{
    return status == RoadPlacementStatus::Allowed || status == RoadPlacementStatus::ExistingRoad ||
        status == RoadPlacementStatus::UpgradeRoad;
}

// Two connected cardinal/diagonal legs, with at most one bend.
// Legacy explicitly constructed paths can still request an orthogonal L.
// World validates endpoints and shape before traversal.
struct RoadPath
{
    int startX = 0, startZ = 0, endX = 0, endZ = 0;
    bool xFirst = true;
    bool diagonal = false;
    bool mixed = false;
    bool diagonalFirst = false;
    bool operator==(const RoadPath&) const = default;
    struct Point { int x, z; };

    // Reach the exact target with min(dx,dz) diagonal steps and the remaining
    // cardinal steps: the shortest route on an eight-direction tile network.
    static RoadPath FromDrag(int x,int z,int targetX,int targetZ)
    {
        const int dx=std::abs(targetX-x),dz=std::abs(targetZ-z);
        RoadPath result{x,z,targetX,targetZ,dx>=dz};
        result.diagonal=dx!=0 && dx==dz;
        result.mixed=dx!=0 && dz!=0 && dx!=dz;
        return result;
    }
    Point Bend() const
    {
        if (diagonal) return {endX,endZ};
        if (mixed)
        {
            const int steps=std::min(std::abs(endX-startX),std::abs(endZ-startZ));
            const int dx=endX>startX ? steps : -steps,dz=endZ>startZ ? steps : -steps;
            return diagonalFirst ? Point{startX+dx,startZ+dz} : Point{endX-dx,endZ-dz};
        }
        return {xFirst ? endX : startX,xFirst ? startZ : endZ};
    }
    static int Distance(Point a,Point b) { return std::max(std::abs(b.x-a.x),std::abs(b.z-a.z)); }
    static bool OnSegment(Point p,Point a,Point b)
    {
        if (p.x<std::min(a.x,b.x) || p.x>std::max(a.x,b.x) || p.z<std::min(a.z,b.z) || p.z>std::max(a.z,b.z)) return false;
        return a.x==b.x || a.z==b.z || std::abs(p.x-a.x)==std::abs(p.z-a.z);
    }
    std::size_t Steps() const
    {
        const auto bend=Bend();
        return static_cast<std::size_t>(Distance({startX,startZ},bend)+Distance(bend,{endX,endZ}));
    }
    // Valid indices are 0..Steps(), inclusive.
    Point TileAt(std::size_t index) const
    {
        const auto bend=Bend();
        const auto first=static_cast<std::size_t>(Distance({startX,startZ},bend));
        const auto a=index<=first ? Point{startX,startZ} : bend;
        const auto b=index<=first ? bend : Point{endX,endZ};
        const int step=static_cast<int>(index<=first ? index : index-first);
        return {a.x+((b.x>a.x)-(b.x<a.x))*step,a.z+((b.z>a.z)-(b.z<a.z))*step};
    }
    bool HasValidShape() const
    {
        // Called after checking bounds, so coordinate differences cannot overflow.
        const int dx=std::abs(endX-startX),dz=std::abs(endZ-startZ);
        if (mixed) return !diagonal && dx && dz && dx!=dz;
        return !diagonal || (dx && dx==dz);
    }
    bool Contains(int x,int z) const
    {
        const auto bend=Bend();
        return OnSegment({x,z},{startX,startZ},bend) || OnSegment({x,z},bend,{endX,endZ});
    }
    // Use consecutive route tiles, not every neighboring tile on either leg.
    // This avoids introducing diagonal shortcuts across an orthogonal corner.
    std::uint8_t ConnectionsAt(int x,int z) const
    {
        if (!Contains(x,z)) return 0;
        const auto index=IndexOf(x,z);
        std::uint8_t ports=0;
        const auto connect=[&](Point p)
        {
            if (p.z==z) ports |= p.x<x ? RoadWest : RoadEast;
            else if (p.x==x) ports |= p.z<z ? RoadNorth : RoadSouth;
            else if (p.z<z) ports |= p.x<x ? RoadNorthWest : RoadNorthEast;
            else ports |= p.x<x ? RoadSouthWest : RoadSouthEast;
        };
        if (index) connect(TileAt(index-1));
        if (index<Steps()) connect(TileAt(index+1));
        return ports;
    }
    // Constant-time lookup into ForEachTile order for either pair of legs.
    std::size_t IndexOf(int x,int z) const
    {
        const auto bend=Bend();
        return static_cast<std::size_t>(OnSegment({x,z},{startX,startZ},bend) ? Distance({startX,startZ},{x,z}) :
            Distance({startX,startZ},bend)+Distance(bend,{x,z}));
    }
    template<class Visitor> void ForEachTile(Visitor visit) const
    {
        const auto steps=Steps();
        for (std::size_t i=0;i<=steps;++i)
        { const auto tile=TileAt(i);visit(tile.x,tile.z); }
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
    std::uint8_t suppressedConnections = 0;
};

// One plan drives evaluation, preview and commit. Only the first continuous
// accepted section in drag order survives; leading blocked tiles are skipped.
// Junctions must use neighbors that actually survive validation and trimming.
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

struct RoadDragPlan
{
    RoadPath path;
    RoadPlan plan;
};
