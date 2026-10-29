#pragma once

#include "ui/minimap_map.hpp"
#include "tool.hpp"
#include "lot.hpp"

#include <span>

class World;
inline constexpr float ZoneOverlayOpacity = 0.65f;

// RGB is the zone color. A: bit 0 = zone eligibility, bits 1..6 =
// ZoneType, bit 7 = an existing road that a route can reuse.
// This is a data texture, NOT an alpha-premultiplied UI image.
std::vector<std::uint8_t> BuildZonePixels(const World& world, ToolTileRect bounds);
// One texel per tile. R: W/N/E/S outline bits; G: arrow facing + 1 (0=none);
// B/A: arrow center relative to this tile, in eighths, biased by +1 tile.
std::vector<std::uint8_t> BuildLotPixels(const World& world);
// Same markings, cropped to the gesture. R bit 4 identifies every planned lot
// tile, including interiors without an outline or arrow. Other pixels are zero.
inline constexpr std::uint8_t LotPreviewTileBit = 16;
std::vector<std::uint8_t> BuildLotPreviewPixels(std::span<const Lot> lots, ToolTileRect bounds);
// Separate from zoning: a slope may accept a road but never a zone. Red stores
// allowed connection bits (0 = blocked). Before any road preview, the complete
// plan fills G with acceptance and B/R/A with the low/middle/high bytes of
// a 24-bit footprint mask (5x5 minus its implicit center).
std::vector<std::uint8_t> BuildRoadPreviewPixels(const World& world, ToolTileRect bounds);
std::vector<std::uint8_t> BuildPlannedRoadPreviewPixels(const World& world, const RoadPlan& plan, ToolTileRect bounds);
MinimapImage BuildZoneMinimapPatch(const MinimapImage& base, const World& world, ToolTileRect bounds);
