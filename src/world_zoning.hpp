#pragma once

#include "tool.hpp"

class World;
class City;

enum class ZoneTileStatus { Change, Unchanged, Blocked };

// Checks all four shared terrain corners, dry land, and occupancy.
bool CanZoneTile(const World& world, int x, int z);
ZoneTileStatus GetZoneTileStatus(const World& world, int x, int z, ZoneType type);

struct ZoneEvaluation
{
    std::uint32_t changes = 0;
    std::uint32_t unchanged = 0;
    std::uint32_t blocked = 0;
    std::int64_t cost = 0;
    bool valid = false;
    bool affordable = false;
};

// Preview and commit use the same rules. Invalid/out-of-map rectangles and
// invalid prices fail without changing World or City. Bounds are inclusive.
ZoneEvaluation EvaluateZoning(const World& world, const ZoneRequest& request, std::int64_t funds);
struct ZonePlacementResult
{
    ZoneEvaluation evaluation;
    std::uint32_t changed = 0;
    std::uint32_t lotsCreated = 0;
};
// Re-evaluates on release. Insufficient funds cancel the whole placement;
// otherwise only eligible changed tiles are charged, once per tile.
ZonePlacementResult ApplyZoning(World& world, City& city, const ZoneRequest& request);
