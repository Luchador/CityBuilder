#pragma once

#include "occupancy.hpp"

// Persistent world data only. The renderer shares one straight mesh between
// all these records; there are no GPU resources or pointers in a Road.
struct Road
{
    InfrastructureId id = NoInfrastructure;
    int tileX = 0;
    int tileZ = 0;
    float elevation = 0.0f;
};

enum class RoadPlacementStatus { Allowed, OutsideMap, NotFlat, Underwater, Occupied };

// Assets use meters, X forward, Y up, with the tile center at X/Z = 0.
inline constexpr char StraightRoadModelPath[] = "models/transportation/roads/road_straight.glb";
inline constexpr char StraightRoadTexturePath[] = "models/transportation/roads/road_straight_basecolor.png";
