#pragma once

#include <cstdint>
#include <string_view>

enum class ZoneType : std::uint8_t
{
    None,

    ResidentialLight,
    ResidentialMedium,
    ResidentialDense,
    ResidentialMixedUse,

    CommercialLight,
    CommercialMedium,
    CommercialDense,

    IndustrialFarm,
    IndustrialMedium,
    IndustrialDense,

    Landfill,

    Seaport
};

enum class ZoneCategory : std::uint8_t
{
    None,
    Residential,
    Commericial,
    Industrial,
    Other
};

struct ZoneDefinition
{
    std::string_view name;
    ZoneCategory category;
};