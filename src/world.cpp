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
        return axis ? CheckDiagonalRoad(x, z, axis) : RoadPlacementStatus::UnsupportedConnection;
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

RoadPlacementStatus World::CheckDiagonalRoad(int x, int z, std::uint8_t axis) const
{
    const auto* existing = GetRoadAt(x, z);
    if (existing && existing->diagonalAxis != axis) return RoadPlacementStatus::UnsupportedConnection;
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
            if (tile.infrastructureId != other.id || other.diagonalAxis != axis)
                return RoadPlacementStatus::Occupied;
            const int deltaX = other.tileX - x, deltaZ = other.tileZ - z;
            if (deltaX != (axis == RoadDiagonalNWSE ? deltaZ : -deltaZ))
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
    if (CheckRoadPlacement(x, z, requiredConnections) != RoadPlacementStatus::Allowed) return false;
    if (nextInfrastructureId == std::numeric_limits<InfrastructureId>::max())
        throw std::overflow_error("No infrastructure IDs remain");
    auto& tile = GetTile(x, z);
    // Allocate before changing occupancy/zoning, so allocation failure is atomic.
    RoadSurface surface;
    ReadRoadSurface(terrain, x, z, surface);
    const auto axis = DiagonalRoadAxis(requiredConnections);
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
    ReconnectRoad(x, z);
    ReconnectRoad(x - 1, z);
    ReconnectRoad(x + 1, z);
    ReconnectRoad(x, z - 1);
    ReconnectRoad(x, z + 1);
    if (axis)
    {
        ReconnectRoad(x - 1, z - 1); ReconnectRoad(x + 1, z - 1);
        ReconnectRoad(x + 1, z + 1); ReconnectRoad(x - 1, z + 1);
    }
    ++roadRevision;
    return true;
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
        const int dx[] = {-1, 1, 1, -1}, dz[] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i)
        {
            const auto bit = static_cast<std::uint8_t>(16 << i);
            if (!(road.diagonalAxis & bit)) continue;
            const auto* neighbor = GetRoadAt(x + dx[i], z + dz[i]);
            if (neighbor && neighbor->diagonalAxis == road.diagonalAxis &&
                neighbor->surface.elevation == road.surface.elevation)
                road.connections |= bit;
        }
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

RoadEvaluation World::EvaluateRoads(const RoadPath& path) const
{
    RoadEvaluation result;
    if (!ContainsTile(path.startX, path.startZ) || !ContainsTile(path.endX, path.endZ) || !path.HasValidShape()) return result;
    result.valid = true;
    path.ForEachTile([&](int x, int z)
    {
        switch (CheckRoadPlacement(x, z, path.ConnectionsAt(x, z)))
        {
            case RoadPlacementStatus::Allowed: ++result.changes; break;
            case RoadPlacementStatus::ExistingRoad: ++result.unchanged; break;
            default: ++result.blocked; break;
        }
    });
    return result;
}

RoadPlacementResult World::PlaceRoads(const RoadPath& path)
{
    RoadPlacementResult result{EvaluateRoads(path)};
    if (!result.evaluation.valid || !result.evaluation.changes) return result;
    if (result.evaluation.changes > std::numeric_limits<InfrastructureId>::max() - nextInfrastructureId)
        throw std::overflow_error("No infrastructure IDs remain");
    // Reserve once before editing; a failed allocation cannot leave half a route.
    const auto needed = roads.size() + result.evaluation.changes;
    if (needed > roads.capacity())
        roads.reserve(std::min(tiles.size(), std::max(needed, roads.capacity() * 2)));
    path.ForEachTile([&](int x, int z) { if (PlaceRoad(x, z, path.ConnectionsAt(x, z))) ++result.changed; });
    return result;
}
