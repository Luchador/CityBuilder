#include "lot.hpp"
#include "world.hpp"
#include "world_zoning.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{
    // Deterministic tie order, independent of drag direction and camera rotation.
    constexpr std::array Facings{LotFacing::South, LotFacing::East, LotFacing::North, LotFacing::West};
    constexpr int RoadDx[]{0, 1, 0, -1}, RoadDz[]{-1, 0, 1, 0};

    ToolTile LotTile(int x, int z, LotFacing facing, int across, int back)
    {
        switch (facing)
        {
            case LotFacing::South: return {x + across, z - back};
            case LotFacing::East: return {x - back, z - across};
            case LotFacing::North: return {x + across, z + back};
            case LotFacing::West: return {x + back, z - across};
        }
        return {x, z};
    }

    struct Candidate
    {
        Lot lot;
        int area = 0, roadContacts = 0, frontage = 1;
    };

    bool Better(const Candidate& a, const Candidate& b)
    {
        if ((a.roadContacts != 0) != (b.roadContacts != 0)) return a.roadContacts != 0;
        if (a.area != b.area) return a.area > b.area;
        // Fraction of the front touching road, without floating-point ties.
        return a.roadContacts * b.frontage > b.roadContacts * a.frontage;
    }
}

std::vector<Lot> PlanZoneLots(const World& world, const ZoneRequest& request)
{
    std::vector<Lot> result;
    const auto& r = request.tiles;
    if (request.zoneType == ZoneType::None) return result;
    if (request.zoneType >= ZoneType::Count || r.minX > r.maxX || r.minZ > r.maxZ ||
        !world.ContainsTile(r.minX, r.minZ) || !world.ContainsTile(r.maxX, r.maxZ))
        throw std::invalid_argument("Invalid lot planning region");
    const auto& definition = Zone::GetZoneDefinition(request.zoneType);
    for (const auto size : {definition.preferredLotSize, definition.maxLotSize})
        if (size && (!size->width || !size->depth)) throw std::invalid_argument("Lot dimensions must be positive");
    const int width = r.maxX - r.minX + 1, depth = r.maxZ - r.minZ + 1;
    std::vector<std::uint8_t> available(static_cast<std::size_t>(width) * depth), roadSides(available.size());
    const auto index = [&](int x, int z) { return static_cast<std::size_t>(z - r.minZ) * width + x - r.minX; };
    const auto free = [&](int x, int z)
    {
        return x >= r.minX && x <= r.maxX && z >= r.minZ && z <= r.maxZ && available[index(x, z)] != 0;
    };
    for (int z = r.minZ; z <= r.maxZ; ++z) for (int x = r.minX; x <= r.maxX; ++x)
    {
        const auto& tile = world.GetTile(x, z);
        if (!CanZoneTile(world, x, z) || (tile.lotId != NoLot && tile.zoneType == request.zoneType)) continue;
        available[index(x, z)] = 1;
        for (int side = 0; side < 4; ++side)
            if (world.GetRoadAt(x + RoadDx[side], z + RoadDz[side]))
                roadSides[index(x, z)] |= static_cast<std::uint8_t>(1 << side);
    }
    const bool light = request.zoneType == ZoneType::ResidentialLight || request.zoneType == ZoneType::CommercialLight;
    const bool single = width == 1 && depth == 1;
    const unsigned extent = static_cast<unsigned>(std::max(width, depth));
    const auto maximum = definition.maxLotSize.value_or(LotSize{extent, extent});
    const auto preferred = definition.preferredLotSize.value_or(maximum);
    const unsigned maxWidth = std::min(maximum.width, extent), maxDepth = std::min(maximum.depth, extent);
    const auto target = request.useMaximumLotSize ? maximum : preferred;
    const int widthLimit = single ? 1 : static_cast<int>(std::min(target.width, maxWidth));
    const int depthLimit = single ? 1 : static_cast<int>(std::min(target.depth, maxDepth));

    std::vector<int> contacts(static_cast<std::size_t>(widthLimit) + 1, 0);
    const auto findCandidate = [&](int x, int z, LotFacing facing, bool absorbRemainder)
    {
        Candidate best;
        int limit = depthLimit;
        if (absorbRemainder && light && !single && !request.useMaximumLotSize)
        {
            // A 3-deep strip becomes one 1x3, not 1x2 + 1x1. On a long strip
            // retain the preferred depth until the final short remainder.
            unsigned run = 0;
            while (run <= maxDepth)
            {
                const auto p = LotTile(x, z, facing, 0, static_cast<int>(run));
                if (!free(p.x, p.z)) break;
                ++run;
            }
            if (run <= maxDepth) limit = static_cast<int>(run);
        }
        int acrossLimit = widthLimit;
        for (int d = 0; d < limit && acrossLimit > 0; ++d)
        {
            int across = 0;
            for (; across < acrossLimit; ++across)
            {
                const auto p = LotTile(x, z, facing, across, d);
                if (!free(p.x, p.z)) break;
                if (!d) contacts[across + 1] = contacts[across] +
                    ((roadSides[index(p.x, p.z)] & (1 << static_cast<unsigned>(facing))) != 0);
            }
            acrossLimit = across;
            if (!across) break;
            const auto far = LotTile(x, z, facing, across - 1, d);
            Candidate candidate;
            candidate.lot.x = std::min(x, far.x); candidate.lot.z = std::min(z, far.z);
            candidate.lot.width = static_cast<unsigned>(std::abs(far.x - x) + 1);
            candidate.lot.depth = static_cast<unsigned>(std::abs(far.z - z) + 1);
            candidate.lot.zoneType = request.zoneType;
            candidate.lot.facing = facing;
            candidate.area = across * (d + 1);
            candidate.frontage = across;
            candidate.roadContacts = contacts[across];
            if (Better(candidate, best)) best = candidate;
        }
        return best;
    };

    // Road frontages get first choice. Then fill interiors with the same
    // deterministic packing rules. No flood fill or scan beyond this gesture.
    for (int pass = 0; pass < 2; ++pass)
        for (int z = r.maxZ; z >= r.minZ; --z) for (int x = r.minX; x <= r.maxX; ++x)
    {
        if (!free(x, z) || (pass == 0 && !roadSides[index(x, z)])) continue;
        Candidate best;
        for (const auto facing : Facings)
        {
            const auto candidate = findCandidate(x, z, facing, false);
            if (Better(candidate, best)) best = candidate;
        }
        if (!best.area || (pass == 0 && !best.roadContacts)) continue;
        // Choose the facing using preferred dimensions first. Extending a short
        // remainder must not turn otherwise regular rows sideways at an edge.
        if (light && !request.useMaximumLotSize)
            best = findCandidate(x, z, best.lot.facing, true);
        const auto& lot = best.lot;
        for (int lz = lot.z; lz < lot.z + static_cast<int>(lot.depth); ++lz)
            for (int lx = lot.x; lx < lot.x + static_cast<int>(lot.width); ++lx)
                available[index(lx, lz)] = 0;
        result.push_back(lot);
    }
    return result;
}
