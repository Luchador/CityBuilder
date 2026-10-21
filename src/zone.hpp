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

    Seaport,

    Count // Table size only; not a zone that can be assigned to a tile.
};

enum class ZoneCategory : std::uint8_t
{
    None,
    Residential,
    Commercial,
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
    // Shared immutable data; no Zone object needs to be constructed.
    // Throws std::out_of_range for Count or an invalid enum value.
    static const ZoneDefinition& GetZoneDefinition(ZoneType type);
};
