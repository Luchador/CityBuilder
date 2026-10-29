#include "lot.hpp"
#include "world.hpp"
#include "world_zoning.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr std::array Facings{LotFacing::South, LotFacing::East, LotFacing::North, LotFacing::West};

    bool AlongZ(LotFacing facing)
    {
        return facing == LotFacing::North || facing == LotFacing::South;
    }

    unsigned Frontage(const Lot& lot, LotFacing facing)
    {
        return AlongZ(facing) ? lot.width : lot.depth;
    }

    bool Fits(const Lot& lot, LotFacing facing, LotSize maximum)
    {
        return Frontage(lot, facing) <= maximum.width &&
            (AlongZ(facing) ? lot.depth : lot.width) <= maximum.depth;
    }

    unsigned RoadContacts(const World& world, const Lot& lot, LotFacing facing)
    {
        unsigned count = 0;
        for (unsigned i = 0; i < Frontage(lot, facing); ++i)
        {
            int x = lot.x, z = lot.z;
            switch (facing)
            {
                case LotFacing::North: x += static_cast<int>(i); --z; break;
                case LotFacing::South: x += static_cast<int>(i); z += static_cast<int>(lot.depth); break;
                case LotFacing::West: --x; z += static_cast<int>(i); break;
                case LotFacing::East: x += static_cast<int>(lot.width); z += static_cast<int>(i); break;
            }
            if (world.GetRoadOccupyingTile(x, z)) ++count;
        }
        return count;
    }

    bool FaceRoad(const World& world, Lot& lot, LotFacing previous, LotSize maximum)
    {
        unsigned bestContacts = 0, bestFrontage = 1;
        // Keep the previous facing when there is no road. Equal road coverage
        // uses the same south/east/north/west order as newly zoned lots.
        lot.facing = previous;
        if (!Fits(lot, previous, maximum))
            for (const auto facing : Facings)
                if (Fits(lot, facing, maximum)) { lot.facing = facing; break; }
        for (const auto facing : Facings)
        {
            if (!Fits(lot, facing, maximum)) continue;
            const auto contacts = RoadContacts(world, lot, facing);
            const auto frontage = Frontage(lot, facing);
            if (contacts * bestFrontage <= bestContacts * frontage) continue;
            lot.facing = facing;
            bestContacts = contacts;
            bestFrontage = frontage;
        }
        return bestContacts != 0;
    }

    // Merge identical runs on consecutive rows. Do this in both orientations
    // and compare the results. Work is linear in footprint area, even for large
    // landfill/seaport lots with many holes; no whole-map search is necessary.
    std::vector<Lot> PackRuns(const Lot& original, std::span<const std::uint8_t> available, bool transpose)
    {
        struct Run { unsigned first, end; std::size_t rectangle; };
        std::vector<Run> previous, current;
        std::vector<Lot> rectangles;
        const auto rows = transpose ? original.width : original.depth;
        const auto columns = transpose ? original.depth : original.width;
        const auto free = [&](unsigned column, unsigned row)
        {
            const auto x = transpose ? row : column, z = transpose ? column : row;
            return available[static_cast<std::size_t>(z) * original.width + x] != 0;
        };
        for (unsigned row = 0; row < rows; ++row)
        {
            current.clear();
            std::size_t old = 0;
            for (unsigned column = 0; column < columns;)
            {
                if (!free(column, row)) { ++column; continue; }
                const auto first = column;
                while (column < columns && free(column, row)) ++column;
                while (old < previous.size() && previous[old].first < first) ++old;
                if (old < previous.size() && previous[old].first == first && previous[old].end == column)
                {
                    const auto index = previous[old].rectangle;
                    if (transpose) ++rectangles[index].width;
                    else ++rectangles[index].depth;
                    current.push_back({first, column, index});
                }
                else
                {
                    Lot lot;
                    lot.zoneType = original.zoneType;
                    lot.x = original.x + static_cast<int>(transpose ? row : first);
                    lot.z = original.z + static_cast<int>(transpose ? first : row);
                    lot.width = transpose ? 1 : column - first;
                    lot.depth = transpose ? column - first : 1;
                    current.push_back({first, column, rectangles.size()});
                    rectangles.push_back(lot);
                }
            }
            previous.swap(current);
        }
        return rectangles;
    }

    struct RepairCandidate
    {
        std::vector<Lot> lots;
        std::size_t servedArea = 0, singleTiles = 0, deepArea = 0, sameFacingArea = 0;
    };

    RepairCandidate MakeCandidate(const World& world, const Lot& original,
        std::span<const std::uint8_t> available, LotSize maximum, bool transpose, bool frontageAlongX)
    {
        RepairCandidate result;
        const bool light = original.zoneType == ZoneType::ResidentialLight || original.zoneType == ZoneType::CommercialLight;
        const unsigned widthLimit = frontageAlongX ? maximum.width : maximum.depth;
        const unsigned depthLimit = frontageAlongX ? maximum.depth : maximum.width;
        for (const auto& rectangle : PackRuns(original, available, transpose))
        {
            // Normally every remnant already fits the original lot's maximum.
            // Also respect the current limits if zone definitions have changed.
            for (unsigned z = 0; z < rectangle.depth; z += depthLimit)
                for (unsigned x = 0; x < rectangle.width; x += widthLimit)
            {
                auto lot = rectangle;
                lot.x += static_cast<int>(x); lot.z += static_cast<int>(z);
                lot.width = std::min(widthLimit, rectangle.width - x);
                lot.depth = std::min(depthLimit, rectangle.depth - z);
                const auto area = static_cast<std::size_t>(lot.width) * lot.depth;
                if (FaceRoad(world, lot, original.facing, maximum)) result.servedArea += area;
                if (area == 1) ++result.singleTiles;
                if (light && Frontage(lot, lot.facing) < (AlongZ(lot.facing) ? lot.depth : lot.width))
                    result.deepArea += area;
                if (lot.facing == original.facing) result.sameFacingArea += area;
                result.lots.push_back(lot);
            }
        }
        return result;
    }

    bool Better(const RepairCandidate& a, const RepairCandidate& b)
    {
        // Prefer access, then avoid unnecessary fragmentation. Narrow/deep
        // light lots and preservation of the old facing break remaining ties.
        if (a.servedArea != b.servedArea) return a.servedArea > b.servedArea;
        if (a.lots.size() != b.lots.size()) return a.lots.size() < b.lots.size();
        if (a.singleTiles != b.singleTiles) return a.singleTiles < b.singleTiles;
        if (a.deepArea != b.deepArea) return a.deepArea > b.deepArea;
        return a.sameFacingArea > b.sameFacingArea;
    }
}

std::vector<Lot> PlanLotRepair(const World& world, const Lot& original, const ZoneRequest* pendingZoning)
{
    if (original.zoneType == ZoneType::None || original.zoneType >= ZoneType::Count ||
        original.buildingId != NoBuilding || original.lotStatus != LotStatus::UNDEVELOPED) return {};
    const auto count = world.GetDimensions().TileCount();
    if (!original.width || !original.depth || original.width > count || original.depth > count ||
        original.facing > LotFacing::West || !world.ContainsTile(original.x, original.z) ||
        !world.ContainsTile(original.x + static_cast<int>(original.width) - 1,
            original.z + static_cast<int>(original.depth) - 1)) throw std::invalid_argument("Invalid lot repair footprint");
    auto maximum = Zone::GetZoneDefinition(original.zoneType).maxLotSize.value_or(LotSize{count, count});
    if (!maximum.width || !maximum.depth) throw std::invalid_argument("Lot dimensions must be positive");
    maximum.width = std::min(maximum.width, count);
    maximum.depth = std::min(maximum.depth, count);
    std::vector<std::uint8_t> available(static_cast<std::size_t>(original.width) * original.depth);
    for (unsigned z = 0; z < original.depth; ++z) for (unsigned x = 0; x < original.width; ++x)
    {
        const int tx = original.x + static_cast<int>(x), tz = original.z + static_cast<int>(z);
        const auto& tile = world.GetTile(tx, tz);
        if (tile.zoneType != original.zoneType || !CanZoneTile(world, tx, tz) ||
            (tile.lotId != NoLot && (!pendingZoning || tile.lotId != original.id))) continue;
        if (pendingZoning && tx >= pendingZoning->tiles.minX && tx <= pendingZoning->tiles.maxX &&
            tz >= pendingZoning->tiles.minZ && tz <= pendingZoning->tiles.maxZ &&
            GetZoneTileStatus(world, tx, tz, pendingZoning->zoneType) == ZoneTileStatus::Change) continue;
        available[static_cast<std::size_t>(z) * original.width + x] = 1;
    }
    auto best = MakeCandidate(world, original, available, maximum, false, AlongZ(original.facing));
    for (const bool transpose : {false, true}) for (const bool frontageAlongX : {true, false})
    {
        if (!transpose && frontageAlongX == AlongZ(original.facing)) continue;
        auto candidate = MakeCandidate(world, original, available, maximum, transpose, frontageAlongX);
        if (Better(candidate, best)) best = std::move(candidate);
    }
    return std::move(best.lots);
}
