#include "world.hpp"
#include "world_zoning.hpp"

#include <stdexcept>

const Lot* World::GetLot(LotId id) const
{
    const auto found = lotIndices.find(id);
    return found == lotIndices.end() ? nullptr : &lots[found->second];
}

const Lot* World::GetLotAt(int x, int z) const
{
    return ContainsTile(x, z) ? GetLot(GetTile(x, z).lotId) : nullptr;
}

LotId World::CreateLot(Lot lot)
{
    const auto count = GetDimensions().TileCount();
    if (lot.zoneType == ZoneType::None || lot.zoneType >= ZoneType::Count ||
        !lot.width || !lot.depth || lot.width > count || lot.depth > count ||
        !ContainsTile(lot.x, lot.z) || !ContainsTile(lot.x + static_cast<int>(lot.width) - 1,
            lot.z + static_cast<int>(lot.depth) - 1) || lot.buildingId != NoBuilding ||
        lot.lotStatus != LotStatus::UNDEVELOPED || lot.facing > LotFacing::West)
        throw std::invalid_argument("Invalid new lot");
    const float height = terrain.Height(lot.x, lot.z);
    for (int z = lot.z; z < lot.z + static_cast<int>(lot.depth); ++z)
        for (int x = lot.x; x < lot.x + static_cast<int>(lot.width); ++x)
    {
        const auto& tile = GetTile(x, z);
        if (!CanZoneTile(*this, x, z) || tile.zoneType != lot.zoneType || tile.lotId != NoLot ||
            terrain.Height(x, z) != height) throw std::invalid_argument("Lot footprint is not available");
    }
    if (nextLotId == NoLot) throw std::overflow_error("No lot IDs remain");
    lot.id = nextLotId;
    lots.push_back(lot);
    try { lotIndices.emplace(lot.id, lots.size() - 1); }
    catch (...) { lots.pop_back(); throw; }
    ++nextLotId;
    for (int z = lot.z; z < lot.z + static_cast<int>(lot.depth); ++z)
        for (int x = lot.x; x < lot.x + static_cast<int>(lot.width); ++x)
            GetTile(x, z).lotId = lot.id;
    ++lotRevision;
    return lot.id;
}

bool World::RemoveLot(LotId id)
{
    const auto found = lotIndices.find(id);

    if (found == lotIndices.end()) 
    {
        return false;
    }

    const auto index = found->second;
    const auto lot = lots[index];

    if (lot.buildingId != NoBuilding || lot.lotStatus != LotStatus::UNDEVELOPED) 
    {
        return false;
    }

    for (int z = lot.z; z < lot.z + static_cast<int>(lot.depth); ++z)
    {
        for (int x = lot.x; x < lot.x + static_cast<int>(lot.width); ++x)
        {
            if (GetTile(x, z).buildingId != NoBuilding) return false;
        }
    }

    for (int z = lot.z; z < lot.z + static_cast<int>(lot.depth); ++z)
    {
        for (int x = lot.x; x < lot.x + static_cast<int>(lot.width); ++x)
        {
            GetTile(x, z).lotId = NoLot;
        }
    }

    // Dense storage can move; public IDs and tile references never do.
    if (index != lots.size() - 1)
    {
        lots[index] = lots.back();
        lotIndices.at(lots[index].id) = index;
    }

    lots.pop_back();
    lotIndices.erase(found);
    ++lotRevision;

    return true;
}

bool World::RemoveLotForRepair(LotId id, std::vector<Lot>& removedLots)
{
    const auto* lot = GetLot(id);

    if (!lot) 
    {
        return false;
    }

    // Copy before RemoveLot's swap/pop invalidates pointers into lots.
    removedLots.push_back(*lot);

    if (RemoveLot(id)) 
    {
        return true;
    }

    removedLots.pop_back();

    return false;
}

std::size_t World::RepairLots(std::span<const Lot> removedLots)
{
    std::size_t created = 0;
    for (const auto& original : removedLots)
    {
        if (GetLot(original.id)) continue; // It was not successfully removed.
        for (const auto& lot : PlanLotRepair(*this, original))
        {
            CreateLot(lot);
            ++created;
        }
    }
    return created;
}
