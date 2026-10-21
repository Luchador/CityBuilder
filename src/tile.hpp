#pragma once

#include "zone.hpp"
#include "occupancy.hpp"

class Tile 
{
public:
    ZoneType zoneType = ZoneType::None;
    BuildingId buildingId = NoBuilding;
    InfrastructureId infrastructureId = NoInfrastructure;

    bool IsOccupied() const
    {
        return buildingId != NoBuilding || infrastructureId != NoInfrastructure;
    }
};
