#include "world.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

void TerrainHeightmap::Validate() const
{
    if (heights.size() != dimensions.CornerCount() || !std::all_of(heights.begin(), heights.end(), [](float h) { return std::isfinite(h) && h <= TerrainMaxElevation; }))
    {
        throw std::invalid_argument("Terrain heightmap must contain (tileCount + 1) squared finite heights at or below 256");
    }
}

World::World(const TerrainSettings& settings) : terrainSettings(settings), terrain(GenerateTerrain(settings)), tiles(static_cast<std::size_t>(terrain.dimensions.TileCount()) * terrain.dimensions.TileCount())
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

RoadPlacementStatus World::CheckRoadPlacement(int x, int z) const
{
    if (!ContainsTile(x, z)) return RoadPlacementStatus::OutsideMap;
    if (GetTile(x, z).IsOccupied()) return RoadPlacementStatus::Occupied;
    const auto ix = static_cast<std::uint32_t>(x), iz = static_cast<std::uint32_t>(z);
    const float corners[] = {terrain.Height(ix, iz), terrain.Height(ix + 1, iz),
                             terrain.Height(ix, iz + 1), terrain.Height(ix + 1, iz + 1)};
    for (const float height : corners)
        if (IsWaterElevation(height)) return RoadPlacementStatus::Underwater;
    for (const float height : corners)
        if (height != corners[0]) return RoadPlacementStatus::NotFlat;
    return RoadPlacementStatus::Allowed;
}

bool World::PlaceRoad(int x, int z)
{
    // Recheck at commit time; a hover preview never authorizes a stale edit.
    if (CheckRoadPlacement(x, z) != RoadPlacementStatus::Allowed) return false;
    if (nextInfrastructureId == std::numeric_limits<InfrastructureId>::max())
        throw std::overflow_error("No infrastructure IDs remain");
    auto& tile = GetTile(x, z);
    // Allocate before changing occupancy/zoning, so allocation failure is atomic.
    roads.push_back({nextInfrastructureId, x, z,
                     terrain.Height(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z))});
    tile.infrastructureId = nextInfrastructureId++;
    tile.zoneType = ZoneType::None;
    return true;
}
