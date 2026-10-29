#include "lot.hpp"
#include "world.hpp"
#include "world_zoning.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

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

static std::vector<Lot> PlanLots(const World& world, const ZoneRequest& request,
    std::optional<LotFacing> preferredFacing = std::nullopt, std::uint8_t* frontageMask = nullptr)
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
            if (world.GetRoadOccupyingTile(x + RoadDx[side], z + RoadDz[side]))
                roadSides[index(x, z)] |= static_cast<std::uint8_t>(1 << side);
        if (frontageMask) *frontageMask |= roadSides[index(x, z)];
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

    // The normal plan keeps its existing scan/tie order. Alternate candidates
    // first grow from one road frontage, then other roads, then interiors.
    // Their scan starts at that frontage, so Alt can change the lot footprints
    // (e.g. vertical versus horizontal strips), not only the front arrows.
    const int roadPasses = preferredFacing ? 2 : 1;
    const auto scanFacing = preferredFacing.value_or(LotFacing::South);
    const bool alongZ = scanFacing == LotFacing::North || scanFacing == LotFacing::South;
    const int anchorX = scanFacing == LotFacing::East ? r.maxX : r.minX;
    const int anchorZ = scanFacing == LotFacing::North ? r.minZ : r.maxZ;
    for (int pass = 0; pass <= roadPasses; ++pass)
        for (int back = 0; back < (alongZ ? depth : width); ++back)
            for (int across = 0; across < (alongZ ? width : depth); ++across)
    {
        const auto tile = LotTile(anchorX, anchorZ, scanFacing, across, back);
        const int x = tile.x, z = tile.z;
        if (!free(x, z) || (pass < roadPasses && !roadSides[index(x, z)])) continue;
        const bool frontagePass = preferredFacing && pass == 0;
        if (frontagePass && !(roadSides[index(x, z)] & (1 << static_cast<unsigned>(*preferredFacing)))) continue;
        Candidate best;
        if (preferredFacing) best = findCandidate(x, z, *preferredFacing, false);
        if (!frontagePass) for (const auto facing : Facings)
        {
            if (preferredFacing == facing) continue;
            const auto candidate = findCandidate(x, z, facing, false);
            if (Better(candidate, best)) best = candidate;
        }
        if (!best.area || (pass < roadPasses && !best.roadContacts)) continue;
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

namespace
{
    struct LayoutScore
    {
        std::size_t roadServedArea = 0, lotCount = 0, singleTiles = 0, deepArea = 0;
        std::uint64_t contacts = 0, frontage = 0;
    };

    LayoutScore ScoreLayout(const World& world, std::span<const Lot> lots)
    {
        LayoutScore score;
        score.lotCount = lots.size();
        for (const auto& lot : lots)
        {
            const bool alongZ = lot.facing == LotFacing::North || lot.facing == LotFacing::South;
            const unsigned width = alongZ ? lot.width : lot.depth;
            const unsigned depth = alongZ ? lot.depth : lot.width;
            const auto area = static_cast<std::size_t>(width) * depth;
            const auto side = static_cast<unsigned>(lot.facing);
            const int x = lot.x + (lot.facing == LotFacing::East ? static_cast<int>(lot.width) - 1 : 0);
            const int z = lot.z + (lot.facing == LotFacing::South ? static_cast<int>(lot.depth) - 1 : 0);
            unsigned contacts = 0;
            for (unsigned i = 0; i < width; ++i)
                if (world.GetRoadOccupyingTile(x + RoadDx[side] + (alongZ ? static_cast<int>(i) : 0),
                    z + RoadDz[side] + (alongZ ? 0 : static_cast<int>(i)))) ++contacts;
            if (contacts) score.roadServedArea += area;
            score.contacts += contacts;
            score.frontage += width;
            if (area == 1) ++score.singleTiles;
            if ((lot.zoneType == ZoneType::ResidentialLight || lot.zoneType == ZoneType::CommercialLight) && depth > width)
                score.deepArea += area;
        }
        return score;
    }

    bool BetterLayout(const LayoutScore& a, const LayoutScore& b)
    {
        if (a.roadServedArea != b.roadServedArea) return a.roadServedArea > b.roadServedArea;
        if (a.lotCount != b.lotCount) return a.lotCount < b.lotCount;
        if (a.singleTiles != b.singleTiles) return a.singleTiles < b.singleTiles;
        const auto firstCoverage = a.contacts * b.frontage, secondCoverage = b.contacts * a.frontage;
        if (firstCoverage != secondCoverage) return firstCoverage > secondCoverage;
        return a.deepArea > b.deepArea;
    }

    // Compare actual footprints AND arrows, independently of creation order.
    // Several frontage passes can converge on the same layout; count it once.
    using LayoutKey = std::vector<std::array<unsigned, 5>>;
    LayoutKey KeyForLayout(std::span<const Lot> lots)
    {
        LayoutKey key;
        key.reserve(lots.size());
        for (const auto& lot : lots)
            key.push_back({static_cast<unsigned>(lot.x), static_cast<unsigned>(lot.z),
                lot.width, lot.depth, static_cast<unsigned>(lot.facing)});
        std::sort(key.begin(), key.end());
        return key;
    }
}

std::vector<Lot> PlanZoneLots(const World& world, const ZoneRequest& request)
{
    std::uint8_t frontageMask = 0;
    auto normal = PlanLots(world, request, std::nullopt, &frontageMask);
    if (normal.empty()) return normal;
    // Keep the common ordinary-zoning path cheap. Competing road frontages
    // need comparison even without Alt, to avoid scan-order bias at corners.
    const bool competingFrontages = (frontageMask & (frontageMask - 1)) != 0;
    if (!request.useAlternateLayout && !competingFrontages) return normal;

    auto bestScore = ScoreLayout(world, normal);
    std::vector<LayoutKey> tiedLayouts;
    if (request.useAlternateLayout) tiedLayouts.push_back(KeyForLayout(normal));
    std::vector<Lot> alternate;
    for (const auto facing : Facings)
    {
        auto candidate = PlanLots(world, request, facing);
        const auto score = ScoreLayout(world, candidate);
        if (BetterLayout(score, bestScore))
        {
            // Ordinary one-frontage/interior zoning keeps the normal plan.
            // Alt must not change it just because a better plan was discovered.
            if (!competingFrontages) return normal;
            normal = std::move(candidate);
            bestScore = score;
            alternate.clear();
            tiedLayouts.clear();
            if (request.useAlternateLayout) tiedLayouts.push_back(KeyForLayout(normal));
            continue;
        }
        if (!request.useAlternateLayout || BetterLayout(bestScore, score)) continue;
        auto key = KeyForLayout(candidate);
        if (std::find(tiedLayouts.begin(), tiedLayouts.end(), key) != tiedLayouts.end()) continue;
        // Three distinct best layouts are enough to disallow the alternate.
        // Continue comparing scores: a later, better plan resets the tie group.
        if (tiedLayouts.size() < 3) tiedLayouts.push_back(std::move(key));
        if (tiedLayouts.size() == 2) alternate = std::move(candidate);
        else alternate.clear();
    }
    return request.useAlternateLayout && tiedLayouts.size() == 2 ? std::move(alternate) : std::move(normal);
}
