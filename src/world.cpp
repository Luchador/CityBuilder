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

World::World(const TerrainSettings& settings) : terrainSettings(settings), terrain(GenerateTerrain(settings)), tiles(static_cast<std::size_t>(terrain.dimensions.TileCount()) * terrain.dimensions.TileCount()), roadAtTile(tiles.size(), 0)
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
    const auto* road = GetRoadAt(x, z);
    const auto& tile = GetTile(x, z);
    if (tile.buildingId != NoBuilding || (!road && tile.infrastructureId != NoInfrastructure))
        return RoadPlacementStatus::Occupied;
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

std::uint8_t World::GetRoadAllowedConnections(int x, int z) const
{
    if (!ContainsTile(x, z) || GetTile(x, z).buildingId != NoBuilding) return 0;
    if (const auto* road = GetRoadAt(x, z)) return road->surface.AllowedConnections();
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
    roads.push_back({nextInfrastructureId, x, z, surface});
    tile.infrastructureId = nextInfrastructureId++;
    tile.zoneType = ZoneType::None;
    roadAtTile[TileIndex(x, z)] = static_cast<std::uint32_t>(roads.size());
    ReconnectRoad(x, z);
    ReconnectRoad(x - 1, z);
    ReconnectRoad(x + 1, z);
    ReconnectRoad(x, z - 1);
    ReconnectRoad(x, z + 1);
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
    const auto connect = [&](int nx, int nz, RoadConnection direction)
    {
        const auto* neighbor = GetRoadAt(nx, nz);
        if (!neighbor || !(road.surface.AllowedConnections() & direction) ||
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
    if (!ContainsTile(path.startX, path.startZ) || !ContainsTile(path.endX, path.endZ)) return result;
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
