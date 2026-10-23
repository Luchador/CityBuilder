#include "world.hpp"
#include "terrain.hpp"
#include "road_footprint.hpp"

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
    if (requiredConnections)
        return PlanRoadsInternal({x, z, x, z}, requiredConnections).tiles.front().status;
    // This hot path also builds the per-tile zone texture. A zero-direction
    // query only checks anchor eligibility; full routes use PlanRoads.
    const auto* road = GetRoadAt(x, z);
    const auto& tile = GetTile(x, z);
    if (tile.buildingId != NoBuilding || (!road && tile.infrastructureId != NoInfrastructure))
        return RoadPlacementStatus::Occupied;
    if (road) return RoadPlacementStatus::ExistingRoad;
    RoadSurface surface;
    return ReadRoadSurface(terrain, x, z, surface);
}

std::uint8_t World::GetRoadAllowedConnections(int x, int z) const
{
    if (!ContainsTile(x, z) || GetTile(x, z).buildingId != NoBuilding) return 0;
    if (const auto* road = GetRoadAt(x, z)) return RoadPorts(*road);
    if (GetTile(x, z).infrastructureId != NoInfrastructure) return 0;
    RoadSurface surface;
    return ReadRoadSurface(terrain, x, z, surface) == RoadPlacementStatus::Allowed ? surface.AllowedConnections() : 0;
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

std::optional<Road> World::PlannedRoadAt(int x, int z, const RoadPath* path, const RoadPlan* plan) const
{
    if (!ContainsTile(x, z)) return std::nullopt;
    const auto* existing = GetRoadAt(x, z);
    if (path && plan && path->Contains(x, z))
    {
        const auto& tile = plan->tiles[path->IndexOf(x, z)];
        if (RoadPlacementAccepted(tile.status))
        {
            Road road;
            if (existing) road = *existing;
            else { road.tileX = x; road.tileZ = z; ReadRoadSurface(terrain, x, z, road.surface); }
            road.diagonalAxis = tile.diagonalAxis;
            road.mixedConnections = tile.mixedConnections;
            return road;
        }
    }
    return existing ? std::optional<Road>(*existing) : std::nullopt;
}

std::uint8_t World::RoadConnectionsAt(const Road& road, const RoadPath* path, const RoadPlan* plan) const
{
    std::uint8_t connections = 0;
    constexpr int dx[] = {-1, 0, 1, 0, -1, 1, 1, -1}, dz[] = {0, -1, 0, 1, -1, -1, 1, 1};
    const auto ports = RoadPorts(road);
    for (int i = 0; i < 8; ++i)
    {
        const auto bit = static_cast<std::uint8_t>(1 << i);
        if (!(ports & bit)) continue;
        const auto neighbor = PlannedRoadAt(road.tileX + dx[i], road.tileZ + dz[i], path, plan);
        if (!neighbor || !(RoadPorts(*neighbor) & RotateRoadConnections(bit, 2))) continue;
        const float edgeX = dx[i] * .5f, edgeZ = dz[i] * .5f;
        if (road.surface.HeightAt(edgeX, edgeZ) == neighbor->surface.HeightAt(-edgeX, -edgeZ)) connections |= bit;
    }
    return connections;
}

RoadPlacementStatus World::CheckRoadFootprint(const Road& road, const RoadPath& path, const RoadPlan& plan) const
{
    const int x = road.tileX, z = road.tileZ;
    const bool flatOnly = road.diagonalAxis || road.mixedConnections;
    for (int pz = z - 1; pz <= z + 1; ++pz) for (int px = x - 1; px <= x + 1; ++px)
    {
        if (!RoadReservesTile(road, px, pz)) continue;
        if (!ContainsTile(px, pz)) return RoadPlacementStatus::OutsideMap;
        const auto index = TileIndex(px, pz);
        const auto& tile = tiles[index];
        if (tile.buildingId != NoBuilding) return RoadPlacementStatus::Occupied;
        if (tile.infrastructureId != NoInfrastructure)
        {
            const auto owner = roadFootprintAtTile[index];
            if (!owner || roads[owner - 1].id != tile.infrastructureId) return RoadPlacementStatus::Occupied;
        }
        RoadSurface surface;
        const auto status = ReadRoadSurface(terrain, px, pz, surface);
        if (status != RoadPlacementStatus::Allowed) return status;
        if (flatOnly && (surface.slopeX != 0 || surface.slopeZ != 0 || surface.elevation != road.surface.elevation))
            return RoadPlacementStatus::NotFlat;
    }
    // Inspect every nearby anchor, not just the first reservation owner. This
    // prevents hiding a collision behind another road sharing the same tile.
    for (int nz = z - 2; nz <= z + 2; ++nz) for (int nx = x - 2; nx <= x + 2; ++nx)
    {
        if (nx == x && nz == z) continue;
        auto other = PlannedRoadAt(nx, nz, &path, &plan);
        if (other) other->connections = RoadConnectionsAt(*other, &path, &plan);
        if (other && RoadMeshesOverlap(road, *other)) return RoadPlacementStatus::Occupied;
    }
    return GetRoadAt(x, z) ? RoadPlacementStatus::ExistingRoad : RoadPlacementStatus::Allowed;
}

RoadPlan World::PlanRoads(const RoadPath& path) const { return PlanRoadsInternal(path, 0); }

RoadPlan World::PlanRoadsInternal(const RoadPath& path, std::uint8_t singleConnections) const
{
    RoadPlan plan;
    if (!ContainsTile(path.startX, path.startZ) || !ContainsTile(path.endX, path.endZ) || !path.HasValidShape()) return plan;
    plan.evaluation.valid = true;
    const auto requiredAt = [&](int x, int z) { return singleConnections ? singleConnections : path.ConnectionsAt(x, z); };
    path.ForEachTile([&](int x, int z)
    {
        const auto required = requiredAt(x, z);
        const auto* existing = GetRoadAt(x, z);
        PlannedRoadTile tile{x, z, existing ? RoadPlacementStatus::ExistingRoad : RoadPlacementStatus::Allowed,
            static_cast<std::uint8_t>((existing ? existing->diagonalAxis : 0) | DiagonalRoadAxes(required)),
            existing ? existing->mixedConnections : std::uint8_t{0}};
        RoadSurface surface;
        const auto status = ReadRoadSurface(terrain, x, z, surface);
        if (status != RoadPlacementStatus::Allowed) tile.status = status;
        else if ((required & 240) && !SupportedDiagonalConnections(required))
            tile.status = RoadPlacementStatus::UnsupportedConnection;
        else if (!(required & 240) && (required & surface.AllowedConnections()) != required)
            tile.status = RoadPlacementStatus::WrongSlopeDirection;
        plan.tiles.push_back(tile);
    });

    // Discover end conversions against the complete proposed path. A temporary
    // all-port probe also finds missing-center junctions. Assign all
    // discoveries together so reversing the drag has identical results.
    auto discovered = plan.tiles;
    for (std::size_t i = 0; i < plan.tiles.size(); ++i)
    {
        const auto& tile = plan.tiles[i];
        if (!RoadPlacementAccepted(tile.status)) continue;
        auto road = *PlannedRoadAt(tile.x, tile.z, &path, &plan);
        const auto required = requiredAt(tile.x, tile.z);
        const auto* existing = GetRoadAt(tile.x, tile.z);
        // A reused diagonal/mixed road must not acquire incidental side arms.
        // Only a route requesting new ports may upgrade an existing module.
        road.diagonalAxis = existing ? static_cast<std::uint8_t>(RoadPorts(*existing) | required) : 255;
        road.mixedConnections = 0; // probe only; never stored in the world
        auto connections = RoadConnectionsAt(road, &path, &plan);
        if (!existing && (required & 240))
        {
            // A nearby grid road running across this port is not an approach.
            // Only discover grid arms that point toward the new diagonal tile.
            constexpr int dx[]{-1, 0, 1, 0}, dz[]{0, -1, 0, 1};
            for (int d = 0; d < 4; ++d) if (connections & (1 << d))
            {
                const auto neighbor = PlannedRoadAt(tile.x + dx[d], tile.z + dz[d], &path, &plan);
                if (!neighbor || neighbor->mixedConnections) continue;
                const auto along = RoadConnectionsAt(*neighbor, &path, &plan);
                const auto axes = along | RotateRoadConnections(along, 2);
                if (axes && !(axes & (1 << d))) connections &= static_cast<std::uint8_t>(~(1 << d));
            }
        }
        // A disconnected requested arm must still constrain the intended shape.
        connections |= required;
        if (existing) connections |= existing->connections;
        auto& proposed = discovered[i];
        if ((connections & 15) && (connections & 240))
        {
            if (SupportedMixedConnections(connections))
            {
                proposed.mixedConnections = connections;
                proposed.diagonalAxis = 0;
            }
            else proposed.status = RoadPlacementStatus::UnsupportedConnection;
        }
        else if (connections & RoadAllDiagonals)
        {
            // Also discover a curve/junction when clicking its missing center.
            // A pair of adjacent diagonal ports needs both corridor axes.
            proposed.diagonalAxis = DiagonalRoadAxes(connections);
        }
    }
    plan.tiles = std::move(discovered);

    // Obstacles can remove approaches. Iterate until every accepted module is
    // valid against exactly the modules that will survive the commit. Rejected
    // upgrades fall back to the existing road; no partial bend is ever installed.
    bool changed;
    do
    {
        changed = false;
        auto checked = plan.tiles;
        for (std::size_t i = 0; i < plan.tiles.size(); ++i)
        {
            const auto& tile = plan.tiles[i];
            if (!RoadPlacementAccepted(tile.status)) continue;
            auto road = *PlannedRoadAt(tile.x, tile.z, &path, &plan);
            road.connections = RoadConnectionsAt(road, &path, &plan);
            auto& proposed = checked[i];
            proposed.status = CheckRoadFootprint(road, path, plan);
            if (!RoadPlacementAccepted(proposed.status)) { changed = true; continue; }
            const auto connections = road.connections;
            const auto* existing = GetRoadAt(tile.x, tile.z);
            const bool layoutChanged = existing && (tile.diagonalAxis != existing->diagonalAxis ||
                tile.mixedConnections != existing->mixedConnections);
            if ((tile.mixedConnections && connections != tile.mixedConnections) ||
                (tile.diagonalAxis && !SupportedDiagonalConnections(connections)) ||
                (layoutChanged && connections == existing->connections))
            {
                proposed.status = RoadPlacementStatus::UnsupportedConnection;
                changed = true;
            }
            else if (tile.diagonalAxis == RoadAllDiagonals)
            {
                if (const auto axis = DiagonalRoadAxis(connections ? connections : requiredAt(tile.x, tile.z)))
                { proposed.diagonalAxis = axis; changed = true; }
            }
            // Connecting a neighbor can widen its mesh even if it is not being
            // placed/upgraded by this path. Check that resulting shape as well.
            if (RoadPlacementAccepted(proposed.status))
            {
                for (int nz = tile.z - 1; nz <= tile.z + 1; ++nz)
                    for (int nx = tile.x - 1; nx <= tile.x + 1; ++nx)
                    {
                        if (path.Contains(nx, nz) && RoadPlacementAccepted(plan.tiles[path.IndexOf(nx, nz)].status)) continue;
                        const auto* old = GetRoadAt(nx, nz);
                        if (!old) continue;
                        auto neighbor = *old;
                        neighbor.connections = RoadConnectionsAt(neighbor, &path, &plan);
                        if (neighbor.connections == old->connections) continue;
                        const auto status = (neighbor.diagonalAxis && !SupportedDiagonalConnections(neighbor.connections)) ||
                            (neighbor.mixedConnections && neighbor.connections != neighbor.mixedConnections) ?
                            RoadPlacementStatus::UnsupportedConnection : CheckRoadFootprint(neighbor, path, plan);
                        if (!RoadPlacementAccepted(status))
                        { proposed.status = status; changed = true; }
                    }
            }
        }
        plan.tiles = std::move(checked);
    } while (changed);

    for (auto& tile : plan.tiles)
    {
        if (RoadPlacementAccepted(tile.status))
        {
            if (const auto* existing = GetRoadAt(tile.x, tile.z))
            {
                const auto road = *PlannedRoadAt(tile.x, tile.z, &path, &plan);
                const auto connections = RoadConnectionsAt(road, &path, &plan);
                if (tile.diagonalAxis != existing->diagonalAxis || tile.mixedConnections != existing->mixedConnections ||
                    (tile.diagonalAxis && connections != existing->connections && std::popcount(connections) >= 3))
                    tile.status = RoadPlacementStatus::UpgradeRoad;
            }
        }
        switch (tile.status)
        {
            case RoadPlacementStatus::UpgradeRoad: ++plan.evaluation.upgrades; ++plan.evaluation.changes; break;
            case RoadPlacementStatus::Allowed: ++plan.evaluation.changes; break;
            case RoadPlacementStatus::ExistingRoad: ++plan.evaluation.unchanged; break;
            default: ++plan.evaluation.blocked; break;
        }
    }
    return plan;
}

RoadEvaluation World::EvaluateRoads(const RoadPath& path) const { return PlanRoads(path).evaluation; }
RoadPlacementResult World::PlaceRoads(const RoadPath& path) { return CommitRoadPlan(PlanRoads(path)); }
bool World::PlaceRoad(int x, int z, std::uint8_t requiredConnections)
{
    return CommitRoadPlan(PlanRoadsInternal({x, z, x, z}, requiredConnections)).changed != 0;
}

void World::InsertRoad(const PlannedRoadTile& tile)
{
    RoadSurface surface;
    ReadRoadSurface(terrain, tile.x, tile.z, surface);
    roads.push_back({nextInfrastructureId++, tile.x, tile.z, surface, 0, tile.diagonalAxis, tile.mixedConnections});
    const auto index = TileIndex(tile.x, tile.z);
    roadAtTile[index] = static_cast<std::uint32_t>(roads.size());
    tiles[index].infrastructureId = roads.back().id;
    ++roadRevision;
}

void World::RefreshRoadReservations(int x, int z)
{
    for (int pz = z - 1; pz <= z + 1; ++pz) for (int px = x - 1; px <= x + 1; ++px)
    {
        if (!ContainsTile(px, pz)) continue;
        const auto index = TileIndex(px, pz);
        const auto oldOwner = roadFootprintAtTile[index];
        auto owner = roadAtTile[index]; // An anchor always owns its own tile.
        if (!owner)
        {
            for (int nz = pz - 1; nz <= pz + 1 && !owner; ++nz)
                for (int nx = px - 1; nx <= px + 1 && !owner; ++nx)
                    if (const auto* road = GetRoadAt(nx, nz); road && RoadReservesTile(*road, px, pz))
                        owner = roadAtTile[TileIndex(nx, nz)];
        }
        if (owner)
        {
            roadFootprintAtTile[index] = owner;
            tiles[index].infrastructureId = roads[owner - 1].id;
            tiles[index].zoneType = ZoneType::None;
        }
        else if (oldOwner)
        {
            roadFootprintAtTile[index] = 0;
            if (tiles[index].infrastructureId == roads[oldOwner - 1].id) tiles[index].infrastructureId = NoInfrastructure;
        }
    }
}

void World::ReconnectRoad(int x, int z)
{
    if (const auto* road = GetRoadAt(x, z))
        roads[roadAtTile[TileIndex(x, z)] - 1].connections = RoadConnectionsAt(*road);
}

void World::ReconnectRoadNeighborhood(int x, int z)
{
    for (int nz = z - 1; nz <= z + 1; ++nz) for (int nx = x - 1; nx <= x + 1; ++nx) ReconnectRoad(nx, nz);
}

RoadPlacementResult World::CommitRoadPlan(const RoadPlan& plan)
{
    RoadPlacementResult result{plan.evaluation};
    if (!result.evaluation.valid || !result.evaluation.changes) return result;
    const auto additions = result.evaluation.changes - result.evaluation.upgrades;
    if (additions > std::numeric_limits<InfrastructureId>::max() - nextInfrastructureId)
        throw std::overflow_error("No infrastructure IDs remain");
    const auto needed = roads.size() + additions;
    if (needed > roads.capacity()) roads.reserve(std::min(tiles.size(), std::max(needed, roads.capacity() * 2)));
    for (const auto& tile : plan.tiles)
    {
        if (tile.status == RoadPlacementStatus::Allowed) { InsertRoad(tile); ++result.changed; }
        else if (tile.status == RoadPlacementStatus::UpgradeRoad)
        {
            auto& road = roads[roadAtTile[TileIndex(tile.x, tile.z)] - 1];
            road.diagonalAxis = tile.diagonalAxis;
            road.mixedConnections = tile.mixedConnections;
            ++roadRevision; ++result.changed;
        }
    }
    for (const auto& tile : plan.tiles)
        if (RoadPlacementAccepted(tile.status)) RefreshRoadReservations(tile.x, tile.z);
    for (const auto& tile : plan.tiles)
        if (RoadPlacementAccepted(tile.status)) ReconnectRoadNeighborhood(tile.x, tile.z);
    return result;
}
