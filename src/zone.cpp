#include "zone.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace
{
    // Keep entries in the same order as ZoneType in zone.hpp.
    // RGB components range from 0 to 1. Costs are placeholder currency units
    // per changed tile; placement validation and charging come later.
    constexpr std::array Definitions{
        //              Name                       Category                   Color (R, G, B)           Cost   preferredLotSize  maxLotSize
        ZoneDefinition{"None",                     ZoneCategory::None,        {0.00f, 0.00f, 0.00f},       0,  std::nullopt,     std::nullopt},

        ZoneDefinition{"Light Residential",        ZoneCategory::Residential, {0.36f, 0.65f, 0.36f},       20,  LotSize{1, 2},   LotSize{3, 3}},
        ZoneDefinition{"Medium Residential",       ZoneCategory::Residential, {0.20f, 0.55f, 0.20f},       40,  LotSize{2, 2},   LotSize{3, 3}},
        ZoneDefinition{"Dense Residential",        ZoneCategory::Residential, {0.04f, 0.40f, 0.10f},      100,  LotSize{3, 3},   LotSize{4, 4}},
        ZoneDefinition{"Mixed-Use Residential",    ZoneCategory::Residential, {0.20f, 0.75f, 0.45f},       60,  LotSize{2, 2},   LotSize{4, 4}},

        ZoneDefinition{"Light Commercial",         ZoneCategory::Commercial,  {0.32f, 0.41f, 1.00f},       20,  LotSize{1, 2},   LotSize{2, 2}},
        ZoneDefinition{"Medium Commercial",        ZoneCategory::Commercial,  {0.13f, 0.20f, 0.90f},       40,  LotSize{2, 2},   LotSize{3, 3}},
        ZoneDefinition{"Dense Commercial",         ZoneCategory::Commercial,  {0.08f, 0.10f, 0.75f},      100,  LotSize{3, 3},   LotSize{4, 4}},

        ZoneDefinition{"Farm",                     ZoneCategory::Industrial,  {0.85f, 0.95f, 0.35f},       20,  LotSize{10, 5},  LotSize{15, 15}},
        ZoneDefinition{"Medium Industrial",        ZoneCategory::Industrial,  {1.00f, 1.00f, 0.15f},       40,  LotSize{2, 2},   LotSize{3, 3}},
        ZoneDefinition{"Dense Industrial",         ZoneCategory::Industrial,  {0.90f, 0.55f, 0.12f},      100,  LotSize{3, 3},   LotSize{4, 4}},

        ZoneDefinition{"Landfill",                 ZoneCategory::Other,       {0.50f, 0.35f, 0.20f},       100, std::nullopt,    std::nullopt},

        ZoneDefinition{"Seaport",                  ZoneCategory::Other,       {0.40f, 0.30f, 0.75f},       500, std::nullopt,    std::nullopt}
    };

    static_assert(Definitions.size() == static_cast<std::size_t>(ZoneType::Count), "Add one zone definition for every ZoneType, in enum order.");
}

const ZoneDefinition& Zone::GetZoneDefinition(ZoneType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= Definitions.size())
    {
        throw std::out_of_range("Unknown zone type");
    }

    return Definitions[index];
}
