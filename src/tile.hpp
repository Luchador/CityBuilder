#pragma once

#include "zone.hpp"
#include "occupancy.hpp"

class Tile 
{
public:
    ZoneType zoneType = ZoneType::None;
    BuildingId buildingId = NoBuilding;
    InfrastructureId infrastructureId = NoInfrastructure;
    LotId lotId = NoLot; // An undeveloped lot does not reserve building occupancy.
    unsigned int LandValue = 0;

    bool IsOccupied() const
    {
        return buildingId != NoBuilding || infrastructureId != NoInfrastructure;
    }
};
