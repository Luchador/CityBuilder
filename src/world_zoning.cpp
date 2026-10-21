#include "world_zoning.hpp"
#include "world.hpp"
#include "city.hpp"

#include <limits>

bool CanZoneTile(const World& world, int x, int z)
{
    if (!world.ContainsTile(x, z) || world.GetTile(x, z).IsOccupied()) return false;
    const auto& terrain = world.GetTerrain();
    const auto ix = static_cast<std::uint32_t>(x), iz = static_cast<std::uint32_t>(z);
    const float height = terrain.Height(ix, iz);
    // Heights are quantized. A slope, even one height step, is not flat.
    return !IsWaterElevation(height) && height == terrain.Height(ix + 1, iz) &&
        height == terrain.Height(ix, iz + 1) && height == terrain.Height(ix + 1, iz + 1);
}

ZoneTileStatus GetZoneTileStatus(const World& world, int x, int z, ZoneType type)
{
    if (!CanZoneTile(world, x, z)) return ZoneTileStatus::Blocked;
    return world.GetTile(x, z).zoneType == type ? ZoneTileStatus::Unchanged : ZoneTileStatus::Change;
}

ZoneEvaluation EvaluateZoning(const World& world, const ZoneRequest& request, std::int64_t funds)
{
    ZoneEvaluation result;
    const auto& r = request.tiles;
    if (request.zoneType >= ZoneType::Count || r.minX > r.maxX || r.minZ > r.maxZ ||
        !world.ContainsTile(r.minX, r.minZ) || !world.ContainsTile(r.maxX, r.maxZ)) return result;
    const auto price = Zone::GetZoneDefinition(request.zoneType).costPerTile;
    if (price < 0) return result;
    for (int z = r.minZ; z <= r.maxZ; ++z)
        for (int x = r.minX; x <= r.maxX; ++x)
            switch (GetZoneTileStatus(world, x, z, request.zoneType))
            {
                case ZoneTileStatus::Change: ++result.changes; break;
                case ZoneTileStatus::Unchanged: ++result.unchanged; break;
                case ZoneTileStatus::Blocked: ++result.blocked; break;
            }
    if (result.changes && price > std::numeric_limits<std::int64_t>::max() / result.changes) return result;
    result.cost = price * result.changes;
    result.valid = true;
    result.affordable = result.cost == 0 || result.cost <= funds;
    return result;
}

ZonePlacementResult ApplyZoning(World& world, City& city, const ZoneRequest& request)
{
    ZonePlacementResult result{EvaluateZoning(world, request, city.funds)};
    if (!result.evaluation.valid || !result.evaluation.affordable) return result;
    // No allocations or fallible operations after validation. Run on the same
    // main thread as simulation/occupancy edits so the quote cannot go stale.
    const auto& r = request.tiles;
    for (int z = r.minZ; z <= r.maxZ; ++z)
        for (int x = r.minX; x <= r.maxX; ++x)
            if (GetZoneTileStatus(world, x, z, request.zoneType) == ZoneTileStatus::Change)
            {
                world.GetTile(x, z).zoneType = request.zoneType;
                ++result.changed;
            }
    city.funds -= result.evaluation.cost;
    return result;
}
