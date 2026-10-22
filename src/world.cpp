#include "world.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    // Coordinates have already been checked. Heights are quantized, so exact
    // equality is intentional. NW/NE/SW/SE are shared terrain corner heights.
    RoadPlacementStatus ReadRoadSurface(const TerrainHeightmap& terrain, int x, int z, RoadSurface& surface)
    {
        const auto ix = static_cast<std::uint32_t>(x), iz = static_cast<std::uint32_t>(z);
        const float nw = terrain.Height(ix, iz), ne = terrain.Height(ix + 1, iz);
        const float sw = terrain.Height(ix, iz + 1), se = terrain.Height(ix + 1, iz + 1);
        if (IsWaterElevation(nw) || IsWaterElevation(ne) || IsWaterElevation(sw) || IsWaterElevation(se))
            return RoadPlacementStatus::Underwater;
        const bool alongX = nw == sw && ne == se;
        const bool alongZ = nw == ne && sw == se;
        if (!alongX && !alongZ) return RoadPlacementStatus::NotFlat;
        surface.slopeX = ne - nw;
        surface.slopeZ = sw - nw;
        surface.elevation = nw + (surface.slopeX + surface.slopeZ) * 0.5f;
        return RoadPlacementStatus::Allowed;
    }
}

void TerrainHeightmap::Validate() const
{
    if (heights.size() != dimensions.CornerCount() || !std::all_of(heights.begin(), heights.end(), [](float h) { return std::isfinite(h) && h <= TerrainMaxElevation; }))
    {
        throw std::invalid_argument("Terrain heightmap must contain (tileCount + 1) squared finite heights at or below 256");
    }
}

World::World(const TerrainSettings& settings) : terrainSettings(settings), terrain(GenerateTerrain(settings)), tiles(static_cast<std::size_t>(terrain.dimensions.TileCount()) * terrain.dimensions.TileCount()), roadAtTile(tiles.size(), 0), roadFootprintAtTile(tiles.size(), 0)
{
    terrain.Validate();
}

bool World::ContainsTile(int x, int z) const
{
    const int count = static_cast<int>(terrain.dimensions.TileCount());

    return x >= 0 && z >= 0 && x < count && z < count;
}

std::size_t World::TileIndex(int x, int z) const
{
    if (!ContainsTile(x, z))
    {
        throw std::out_of_range("Tile coordinates are outside the world");
    }

    return static_cast<std::size_t>(z) * terrain.dimensions.TileCount() + static_cast<std::size_t>(x);
}

Tile& World::GetTile(int x, int z)
{
    return tiles[TileIndex(x, z)];
}

const Tile& World::GetTile(int x, int z) const
{
    return tiles[TileIndex(x, z)];
}

RoadPlacementStatus World::CheckRoadPlacement(int x, int z, std::uint8_t requiredConnections) const
{
    if (!ContainsTile(x, z)) return RoadPlacementStatus::OutsideMap;
    if (requiredConnections & 240)
    {
        const auto axis = DiagonalRoadAxis(requiredConnections);
        if (!axis) return RoadPlacementStatus::UnsupportedConnection;
        const auto status = CheckDiagonalFootprint(x, z);
        if (!RoadPlacementAccepted(status)) return status;
        const auto* road = GetRoadAt(x, z);
        if (!road || (road->diagonalAxis & axis) == axis) return status;
        const auto connections = DiagonalConnectionsAt(x, z, static_cast<std::uint8_t>(road->diagonalAxis | axis));
        // A direct one-tile upgrade can only use neighbors already present.
        // PlanRoads also considers the new neighbors in a complete drag.
        return connections != road->connections && SupportedDiagonalConnections(connections) ?
            RoadPlacementStatus::UpgradeRoad : RoadPlacementStatus::UnsupportedConnection;
    }
    const auto* road = GetRoadAt(x, z);
    const auto& tile = GetTile(x, z);
    if (tile.buildingId != NoBuilding || (!road && tile.infrastructureId != NoInfrastructure))
        return RoadPlacementStatus::Occupied;
    if (road && road->diagonalAxis)
        return requiredConnections ? RoadPlacementStatus::UnsupportedConnection : RoadPlacementStatus::ExistingRoad;
    RoadSurface surface;
    if (road) surface = road->surface;
    else
    {
        const auto status = ReadRoadSurface(terrain, x, z, surface);
        if (status != RoadPlacementStatus::Allowed) return status;
    }
    if ((requiredConnections & surface.AllowedConnections()) != requiredConnections)
        return RoadPlacementStatus::WrongSlopeDirection;
    return road ? RoadPlacementStatus::ExistingRoad : RoadPlacementStatus::Allowed;
}

RoadPlacementStatus World::CheckDiagonalFootprint(int x, int z) const
{
    if (!ContainsTile(x, z)) return RoadPlacementStatus::OutsideMap;
    const auto* existing = GetRoadAt(x, z);
    if (existing && !existing->diagonalAxis) return RoadPlacementStatus::UnsupportedConnection;
    RoadSurface center;
    const auto centerStatus = ReadRoadSurface(terrain, x, z, center);
    if (centerStatus != RoadPlacementStatus::Allowed) return centerStatus;
    if (center.slopeX != 0 || center.slopeZ != 0) return RoadPlacementStatus::NotFlat;

    // A stretched/rotated one-tile mesh overlaps the four side neighbors, but
    // never the four diagonal neighbors. Reserve that conservative five-tile
    // footprint even for ends, so extending an end cannot grow into a building.
    constexpr int dx[] = {0, -1, 0, 1, 0}, dz[] = {0, 0, -1, 0, 1};
    for (int i = 0; i < 5; ++i)
    {
        const int px = x + dx[i], pz = z + dz[i];
        if (!ContainsTile(px, pz)) return RoadPlacementStatus::OutsideMap;
        const auto index = TileIndex(px, pz);
        const auto& tile = tiles[index];
        if (tile.buildingId != NoBuilding) return RoadPlacementStatus::Occupied;
        if (tile.infrastructureId != NoInfrastructure)
        {
            const auto owner = roadFootprintAtTile[index];
            if (!owner) return RoadPlacementStatus::Occupied;
            const auto& other = roads[owner - 1];
            if (tile.infrastructureId != other.id || !other.diagonalAxis)
                return RoadPlacementStatus::Occupied;
            // Each diagonal mesh fits |localX| + |localZ| <= 1 after yaw.
            // Those rotated cells tile without overlap for anchors of equal
            // parity. Opposite parity would cross between tile centers.
            if (((other.tileX + other.tileZ) & 1) != ((x + z) & 1))
                return RoadPlacementStatus::Occupied;
        }
        RoadSurface surface;
        const auto status = ReadRoadSurface(terrain, px, pz, surface);
        if (status != RoadPlacementStatus::Allowed) return status;
        if (surface.slopeX != 0 || surface.slopeZ != 0 || surface.elevation != center.elevation)
            return RoadPlacementStatus::NotFlat;
    }
    return existing ? RoadPlacementStatus::ExistingRoad : RoadPlacementStatus::Allowed;
}

std::uint8_t World::GetRoadAllowedConnections(int x, int z) const
{
    if (!ContainsTile(x, z) || GetTile(x, z).buildingId != NoBuilding) return 0;
    if (const auto* road = GetRoadAt(x, z)) return road->diagonalAxis ? road->diagonalAxis : road->surface.AllowedConnections();
    if (GetTile(x, z).infrastructureId != NoInfrastructure) return 0;
    RoadSurface surface;
    return ReadRoadSurface(terrain, x, z, surface) == RoadPlacementStatus::Allowed ? surface.AllowedConnections() : 0;
}

bool World::PlaceRoad(int x, int z, std::uint8_t requiredConnections)
{
    // Recheck at commit time; a hover preview never authorizes a stale edit.
    const auto status = CheckRoadPlacement(x, z, requiredConnections);
    if (status == RoadPlacementStatus::UpgradeRoad)
    {
        auto& road = roads[roadAtTile[TileIndex(x, z)] - 1];
        road.diagonalAxis |= DiagonalRoadAxis(requiredConnections);
        if (const auto axis = DiagonalRoadAxis(DiagonalConnectionsAt(x, z, road.diagonalAxis)))
            road.diagonalAxis = axis;
        ++roadRevision;
    }
    else if (status == RoadPlacementStatus::Allowed)
    {
        InsertRoad(x, z, DiagonalRoadAxis(requiredConnections));
    }
    else return false;
    ReconnectRoadNeighborhood(x, z);
    return true;
}

void World::InsertRoad(int x, int z, std::uint8_t axis)
{
    if (nextInfrastructureId == std::numeric_limits<InfrastructureId>::max())
        throw std::overflow_error("No infrastructure IDs remain");
    auto& tile = GetTile(x, z);
    // Allocate before changing occupancy/zoning, so allocation failure is atomic.
    RoadSurface surface;
    ReadRoadSurface(terrain, x, z, surface);
    roads.push_back({nextInfrastructureId, x, z, surface, 0, axis});
    tile.infrastructureId = nextInfrastructureId++;
    tile.zoneType = ZoneType::None;
    roadAtTile[TileIndex(x, z)] = static_cast<std::uint32_t>(roads.size());
    roadFootprintAtTile[TileIndex(x, z)] = static_cast<std::uint32_t>(roads.size());
    if (axis)
    {
        constexpr int dx[] = {-1, 0, 1, 0}, dz[] = {0, -1, 0, 1};
        for (int i = 0; i < 4; ++i)
        {
            const auto index = TileIndex(x + dx[i], z + dz[i]);
            if (!roadFootprintAtTile[index])
            {
                roadFootprintAtTile[index] = static_cast<std::uint32_t>(roads.size());
                tiles[index].infrastructureId = roads.back().id;
            }
            tiles[index].zoneType = ZoneType::None;
        }
    }
    ++roadRevision;
}

void World::ReconnectRoadNeighborhood(int x, int z)
{
    ReconnectRoad(x, z);
    ReconnectRoad(x - 1, z);
    ReconnectRoad(x + 1, z);
    ReconnectRoad(x, z - 1);
    ReconnectRoad(x, z + 1);
    ReconnectRoad(x - 1, z - 1); ReconnectRoad(x + 1, z - 1);
    ReconnectRoad(x + 1, z + 1); ReconnectRoad(x - 1, z + 1);
}

const Road* World::GetRoadAt(int x, int z) const
{
    if (!ContainsTile(x, z)) return nullptr;
    const auto index = TileIndex(x, z);
    const auto entry = roadAtTile[index];
    if (!entry) return nullptr;
    const auto& road = roads[entry - 1];
    return tiles[index].infrastructureId == road.id ? &road : nullptr;
}

void World::ReconnectRoad(int x, int z)
{
    if (!GetRoadAt(x, z)) return;
    auto& road = roads[roadAtTile[TileIndex(x, z)] - 1];
    road.connections = 0;
    if (road.diagonalAxis)
    {
        road.connections = DiagonalConnectionsAt(x, z, road.diagonalAxis);
        return;
    }
    const auto connect = [&](int nx, int nz, RoadConnection direction)
    {
        const auto* neighbor = GetRoadAt(nx, nz);
        if (!neighbor || neighbor->diagonalAxis || !(road.surface.AllowedConnections() & direction) ||
            !(neighbor->surface.AllowedConnections() & RotateRoadConnections(direction, 2))) return;
        const float edgeX = static_cast<float>(nx - x) * 0.5f;
        const float edgeZ = static_cast<float>(nz - z) * 0.5f;
        // Centers can have different heights on a hill. Compare the shared
        // level edge instead. Parallel hillside roads do not connect sideways.
        if (road.surface.HeightAt(edgeX, edgeZ) == neighbor->surface.HeightAt(-edgeX, -edgeZ))
            road.connections |= direction;
    };
    connect(x - 1, z, RoadWest);
    connect(x, z - 1, RoadNorth);
    connect(x + 1, z, RoadEast);
    connect(x, z + 1, RoadSouth);
}

std::uint8_t World::DiagonalConnectionsAt(int x, int z, std::uint8_t axes,
    const RoadPath* path, const RoadPlan* plan) const
{
    const auto* center = GetRoadAt(x, z);
    const float elevation = center ? center->surface.elevation : terrain.Height(x, z);
    std::uint8_t connections = 0;
    constexpr int dx[] = {-1, 1, 1, -1}, dz[] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i)
    {
        const auto bit = static_cast<std::uint8_t>(16 << i);
        if (!(axes & bit)) continue;
        const int nx = x + dx[i], nz = z + dz[i];
        if (!ContainsTile(nx, nz)) continue;
        const auto* neighbor = GetRoadAt(nx, nz);
        auto neighborAxes = neighbor ? neighbor->diagonalAxis : std::uint8_t{0};
        const float neighborHeight = neighbor ? neighbor->surface.elevation : terrain.Height(nx, nz);
        if (path && plan && path->Contains(nx, nz))
        {
            const auto& proposed = plan->tiles[static_cast<std::size_t>(std::abs(nx - path->startX))];
            if (RoadPlacementAccepted(proposed.status)) neighborAxes = proposed.diagonalAxis;
        }
        if ((neighborAxes & RotateRoadConnections(bit, 2)) && neighborHeight == elevation)
            connections |= bit;
    }
    return connections;
}

RoadPlan World::PlanRoads(const RoadPath& path) const
{
    RoadPlan result;
    if (!ContainsTile(path.startX, path.startZ) || !ContainsTile(path.endX, path.endZ) || !path.HasValidShape()) return result;
    result.evaluation.valid = true;
    path.ForEachTile([&](int x, int z)
    {
        const auto required = path.ConnectionsAt(x, z);
        const auto* existing = GetRoadAt(x, z);
        const auto axes = static_cast<std::uint8_t>((existing ? existing->diagonalAxis : 0) | DiagonalRoadAxis(required));
        result.tiles.push_back({x, z, path.diagonal ? CheckDiagonalFootprint(x, z) : CheckRoadPlacement(x, z, required), axes});
    });
    if (path.diagonal)
    {
        // Filling a missing center between existing approaches may also create
        // a junction. Only consider neighbors whose ports actually face it.
        for (auto& tile : result.tiles)
        {
            if (tile.status != RoadPlacementStatus::Allowed) continue;
            const auto connections = DiagonalConnectionsAt(tile.x, tile.z, RoadAllDiagonals, &path, &result);
            if ((connections & tile.diagonalAxis) && (connections & ~tile.diagonalAxis))
            {
                if (SupportedDiagonalConnections(connections)) tile.diagonalAxis = RoadAllDiagonals;
                else tile.status = RoadPlacementStatus::UnsupportedConnection;
            }
        }
        // Remove upgrades that would leave an unsupported two-port bend after
        // blocked approaches are skipped. Repeat because removing an upgrade
        // can change the neighbors available to another planned junction.
        bool removed;
        do
        {
            removed = false;
            for (auto& tile : result.tiles)
            {
                if (!RoadPlacementAccepted(tile.status)) continue;
                const auto connections = DiagonalConnectionsAt(tile.x, tile.z, tile.diagonalAxis, &path, &result);
                const auto* existing = GetRoadAt(tile.x, tile.z);
                if (!SupportedDiagonalConnections(connections) ||
                    (existing && tile.diagonalAxis != existing->diagonalAxis && connections == existing->connections))
                {
                    tile.status = RoadPlacementStatus::UnsupportedConnection;
                    removed = true;
                }
                else if (tile.diagonalAxis == RoadAllDiagonals)
                {
                    // A partial drag may leave only a straight/end. Do not
                    // retain unused perpendicular ports that could later grow
                    // an unsupported bend from a nearby, unrelated placement.
                    if (const auto axis = DiagonalRoadAxis(connections ? connections : path.ConnectionsAt(tile.x, tile.z)))
                    {
                        tile.diagonalAxis = axis;
                        removed = true;
                    }
                }
            }
        }
        while (removed);
        for (auto& tile : result.tiles)
        {
            if (!RoadPlacementAccepted(tile.status)) continue;
            if (const auto* existing = GetRoadAt(tile.x, tile.z))
            {
                const auto connections = DiagonalConnectionsAt(tile.x, tile.z, tile.diagonalAxis, &path, &result);
                if (tile.diagonalAxis != existing->diagonalAxis ||
                    (connections != existing->connections && std::popcount(connections) >= 3))
                    tile.status = RoadPlacementStatus::UpgradeRoad;
            }
        }
    }
    for (const auto& tile : result.tiles)
    {
        switch (tile.status)
        {
            case RoadPlacementStatus::UpgradeRoad: ++result.evaluation.upgrades; ++result.evaluation.changes; break;
            case RoadPlacementStatus::Allowed: ++result.evaluation.changes; break;
            case RoadPlacementStatus::ExistingRoad: ++result.evaluation.unchanged; break;
            default: ++result.evaluation.blocked; break;
        }
    }
    return result;
}

RoadEvaluation World::EvaluateRoads(const RoadPath& path) const
{
    return PlanRoads(path).evaluation;
}

RoadPlacementResult World::PlaceRoads(const RoadPath& path)
{
    const auto plan = PlanRoads(path);
    RoadPlacementResult result{plan.evaluation};
    if (!result.evaluation.valid || !result.evaluation.changes) return result;
    const auto additions = result.evaluation.changes - result.evaluation.upgrades;
    if (additions > std::numeric_limits<InfrastructureId>::max() - nextInfrastructureId)
        throw std::overflow_error("No infrastructure IDs remain");
    // Reserve once before editing; a failed allocation cannot leave half a route.
    const auto needed = roads.size() + additions;
    if (needed > roads.capacity())
        roads.reserve(std::min(tiles.size(), std::max(needed, roads.capacity() * 2)));
    // Apply the complete plan before reconnecting. Checking against a partially
    // committed path would incorrectly reject a junction missing its next arm.
    for (const auto& tile : plan.tiles)
    {
        if (tile.status == RoadPlacementStatus::Allowed)
        {
            InsertRoad(tile.x, tile.z, tile.diagonalAxis);
            ++result.changed;
        }
        else if (tile.status == RoadPlacementStatus::UpgradeRoad)
        {
            roads[roadAtTile[TileIndex(tile.x, tile.z)] - 1].diagonalAxis = tile.diagonalAxis;
            ++roadRevision;
            ++result.changed;
        }
    }
    for (const auto& tile : plan.tiles)
        if (RoadPlacementAccepted(tile.status)) ReconnectRoadNeighborhood(tile.x, tile.z);
    return result;
}
