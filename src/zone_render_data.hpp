#pragma once

#include "minimap_map.hpp"
#include "tool.hpp"

class World;
inline constexpr float ZoneOverlayOpacity = 0.65f;

// RGB is the zone color. A: bit 0 = zone/empty-road eligibility, bits 1..6 =
// ZoneType, bit 7 = an existing road that a route can reuse.
// This is a data texture, NOT an alpha-premultiplied UI image.
std::vector<std::uint8_t> BuildZonePixels(const World& world, ToolTileRect bounds);
MinimapImage BuildZoneMinimapPatch(const MinimapImage& base, const World& world, ToolTileRect bounds);
