/**
 * A Lot is comprised of one or more Zone tiles, forming a square or rectangle. The terrain beneath them must be flat.
 * Different zones have different preferred Lot sizes. In high land value areas Lots can combine to form larger Lots.
 */

#pragma once

#include <cstdint>
#include "building.hpp"
#include "zone.hpp"

enum class LotStatus
{
    UNDEVELOPED,
    CONSTRUCTION,
    BUILT,
    ABANDONED,
    DESTROYED
};

using LotId = std::uint32_t;
inline constexpr LotId NoLot = 0;

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