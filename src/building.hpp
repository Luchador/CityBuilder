#pragma once

enum BuildingStatus
{
    CONSTRUCTION,
    BUILT,
    ABANDONED,
    DESTROYED
};

class Building
{
    enum BuildingStatus buildingStatus = BUILT;
};