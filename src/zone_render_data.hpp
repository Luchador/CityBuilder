#pragma once

#include "minimap_map.hpp"
#include "tool.hpp"

class World;
inline constexpr float ZoneOverlayOpacity = 0.65f;

// RGB is the zone color. A encodes 2 * ZoneType + placement-eligible bit.
// This is a data texture, NOT an alpha-premultiplied UI image.
std::vector<std::uint8_t> BuildZonePixels(const World& world, ToolTileRect bounds);
MinimapImage BuildZoneMinimapPatch(const MinimapImage& base, const World& world, ToolTileRect bounds);
