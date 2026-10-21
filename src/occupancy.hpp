#pragma once

#include <cstdint>

// Zero means empty. Future World-owned building/network records will supply
// stable IDs; every tile in an object's footprint references that same ID.
// These handles reserve occupancy without allocating an object per tile.
using BuildingId = std::uint32_t;
using InfrastructureId = std::uint32_t;
inline constexpr BuildingId NoBuilding = 0;
inline constexpr InfrastructureId NoInfrastructure = 0;
