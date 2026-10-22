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
        //              Name                       Category                   Color (R, G, B)           Cost
        ZoneDefinition{"None",                     ZoneCategory::None,        {0.00f, 0.00f, 0.00f},       0},

        ZoneDefinition{"Light Residential",        ZoneCategory::Residential, {0.36f, 0.60f, 0.33f},       20},
        ZoneDefinition{"Medium Residential",       ZoneCategory::Residential, {0.25f, 0.70f, 0.20f},       40},
        ZoneDefinition{"Dense Residential",        ZoneCategory::Residential, {0.08f, 0.45f, 0.12f},      100},
        ZoneDefinition{"Mixed-Use Residential",    ZoneCategory::Residential, {0.20f, 0.75f, 0.65f},       60},

        ZoneDefinition{"Light Commercial",         ZoneCategory::Commercial,  {0.45f, 0.75f, 1.00f},       20},
        ZoneDefinition{"Medium Commercial",        ZoneCategory::Commercial,  {0.20f, 0.45f, 0.90f},       40},
        ZoneDefinition{"Dense Commercial",         ZoneCategory::Commercial,  {0.10f, 0.20f, 0.65f},      100},

        ZoneDefinition{"Farm",                     ZoneCategory::Industrial,  {0.85f, 0.75f, 0.35f},       20},
        ZoneDefinition{"Medium Industrial",        ZoneCategory::Industrial,  {1.00f, 0.85f, 0.15f},       40},
        ZoneDefinition{"Dense Industrial",         ZoneCategory::Industrial,  {0.90f, 0.55f, 0.08f},      100},

        ZoneDefinition{"Landfill",                 ZoneCategory::Other,       {0.50f, 0.35f, 0.20f},       100},

        ZoneDefinition{"Seaport",                  ZoneCategory::Other,       {0.40f, 0.30f, 0.75f},       500}
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
