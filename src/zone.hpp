#pragma once

#include <cstdint>
#include <string_view>

struct ZoneColor
{
    float r;
    float g;
    float b;
};

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
    ZoneColor color;
    std::int64_t costPerTile;
};

class Zone
{
public:
    const ZoneDefinition& GetZoneDefinition(ZoneType type);
};