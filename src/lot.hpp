/**
 * A Lot is comprised of one or more Zone tiles, forming a square or rectangle. The terrain beneath them must be flat.
 * Different zones have different preferred Lot sizes. In high land value areas Lots can combine to form larger Lots.
 */

#pragma once

#include <cstdint>
#include "occupancy.hpp"
#include "zone.hpp"
#include <vector>

enum class LotStatus
{
    UNDEVELOPED,
    CONSTRUCTION,
    BUILT,
    ABANDONED,
    DESTROYED
};

// World directions: north is -Z, east is +X. Camera rotation does not change these.
enum class LotFacing : std::uint8_t
{
    North,
    East,
    South,
    West
};

class Lot
{
    public:
        LotId id = NoLot;
        ZoneType zoneType = ZoneType::None;

        int x = 0;
        int z = 0;

        unsigned int width = 1; // Tile count along world X.
        unsigned int depth = 1; // Tile count along world Z.

        LotFacing facing = LotFacing::North;
        BuildingId buildingId = NoBuilding;
        enum LotStatus lotStatus = LotStatus::UNDEVELOPED;
};

class World;
struct ZoneRequest;
// Plans only inside this gesture. Existing same-zone lots are left intact.
// Preferred/max dimensions are frontage width and depth away from the road;
// Lot::width/depth above are the resulting world X/Z footprint.
// Alt compares distinct cardinal-frontage layouts against the normal plan and
// uses the other equally ranked layout only in an exact two-way tie.
std::vector<Lot> PlanZoneLots(const World& world, const ZoneRequest& request);

// Repack only the original lot's surviving tiles, retaining large rectangles
// instead of reverting to preferred sizes. Optional zoning is previewed without
// editing World. Call after removing the original lot when committing repairs.
std::vector<Lot> PlanLotRepair(const World& world, const Lot& original,
    const ZoneRequest* pendingZoning = nullptr);
