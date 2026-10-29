#include "zone_render_data.hpp"
#include "world_zoning.hpp"
#include "world.hpp"
#include "road_footprint.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    void CheckBounds(const World& world, ToolTileRect r)
    {
        if (r.minX > r.maxX || r.minZ > r.maxZ || !world.ContainsTile(r.minX, r.minZ) ||
            !world.ContainsTile(r.maxX, r.maxZ)) throw std::invalid_argument("Invalid zone texture region");
    }
    std::uint8_t Byte(float color)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(color, 0.0f, 1.0f) * 255.0f));
    }
}

std::vector<std::uint8_t> BuildLotPixels(const World& world)
{
    const auto count = world.GetDimensions().TileCount();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(count) * count * 4, 0);
    for (const auto& lot : world.GetLots())
    {
        if (lot.buildingId != NoBuilding || lot.lotStatus != LotStatus::UNDEVELOPED) continue;
        const int endX = lot.x + static_cast<int>(lot.width), endZ = lot.z + static_cast<int>(lot.depth);
        float centerX = lot.x + lot.width * 0.5f, centerZ = lot.z + lot.depth * 0.5f;
        // Arrow quad is 0.75 tile wide, centered 0.375 tile inside the front.
        // Half/eighth-tile coordinates pack exactly into the data texture.
        switch (lot.facing)
        {
            case LotFacing::North: centerZ = lot.z + 0.375f; break;
            case LotFacing::East: centerX = endX - 0.375f; break;
            case LotFacing::South: centerZ = endZ - 0.375f; break;
            case LotFacing::West: centerX = lot.x + 0.375f; break;
        }
        for (int z = lot.z; z < endZ; ++z) for (int x = lot.x; x < endX; ++x)
        {
            const auto i = (static_cast<std::size_t>(z) * count + x) * 4;
            pixels[i] = static_cast<std::uint8_t>((x == lot.x ? 1 : 0) | (z == lot.z ? 2 : 0) |
                (x == endX - 1 ? 4 : 0) | (z == endZ - 1 ? 8 : 0));
            if (centerX + 0.375f <= x || centerX - 0.375f >= x + 1 ||
                centerZ + 0.375f <= z || centerZ - 0.375f >= z + 1) continue;
            pixels[i + 1] = static_cast<std::uint8_t>(lot.facing) + 1;
            pixels[i + 2] = static_cast<std::uint8_t>(std::lround((centerX - x + 1.0f) * 8.0f));
            pixels[i + 3] = static_cast<std::uint8_t>(std::lround((centerZ - z + 1.0f) * 8.0f));
        }
    }
    return pixels;
}

std::vector<std::uint8_t> BuildZonePixels(const World& world, ToolTileRect r)
{
    static_assert(static_cast<unsigned int>(ZoneType::Count) <= 64, "Zone texture metadata needs another channel");
    CheckBounds(world, r);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(r.maxX - r.minX + 1) * (r.maxZ - r.minZ + 1) * 4);
    std::size_t offset = 0;
    for (int z = r.minZ; z <= r.maxZ; ++z)
        for (int x = r.minX; x <= r.maxX; ++x)
        {
            const auto type = world.GetTile(x, z).zoneType;
            const auto color = Zone::GetZoneDefinition(type).color;
            pixels[offset++] = Byte(color.r);
            pixels[offset++] = Byte(color.g);
            pixels[offset++] = Byte(color.b);
            pixels[offset++] = static_cast<std::uint8_t>(2 * static_cast<unsigned int>(type) +
                (CanZoneTile(world, x, z) ? 1 : 0) +
                (world.CheckRoadPlacement(x, z) == RoadPlacementStatus::ExistingRoad ? 128 : 0));
        }
    return pixels;
}

std::vector<std::uint8_t> BuildRoadPreviewPixels(const World& world, ToolTileRect r)
{
    CheckBounds(world, r);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(r.maxX - r.minX + 1) * (r.maxZ - r.minZ + 1) * 4, 0);
    std::size_t offset = 0;
    for (int z = r.minZ; z <= r.maxZ; ++z)
    {
        for (int x = r.minX; x <= r.maxX; ++x)
        {
            pixels[offset] = world.GetRoadAllowedConnections(x, z);
            pixels[offset + 3] = 255;
            offset += 4;
        }
    }
    return pixels;
}

std::vector<std::uint8_t> BuildPlannedRoadPreviewPixels(const World& world, const RoadPlan& plan, ToolTileRect r)
{
    auto pixels = BuildRoadPreviewPixels(world, r);
    for (const auto& tile : plan.tiles)
    {
        if (tile.x < r.minX || tile.x > r.maxX || tile.z < r.minZ || tile.z > r.maxZ) continue;
        const auto offset = (static_cast<std::size_t>(tile.z - r.minZ) * (r.maxX - r.minX + 1) + tile.x - r.minX) * 4;
        pixels[offset + 1] = RoadPlacementAccepted(tile.status) ? 255 : 0;
        const auto footprint = RoadFootprintMask(tile.diagonalAxis, tile.mixedConnections);
        pixels[offset + 2] = static_cast<std::uint8_t>(footprint);
        pixels[offset] = static_cast<std::uint8_t>(footprint >> 8);
        pixels[offset + 3] = static_cast<std::uint8_t>(footprint >> 16);
    }
    return pixels;
}

MinimapImage BuildZoneMinimapPatch(const MinimapImage& base, const World& world, ToolTileRect r)
{
    CheckBounds(world, r);
    const auto count = world.GetDimensions().TileCount();
    if (!base.width || base.width != base.height || base.width % count ||
        base.rgba.size() != static_cast<std::size_t>(base.width) * base.height * 4)
        throw std::invalid_argument("Invalid base minimap for zone overlay");
    const auto scale = base.width / count;
    MinimapImage patch;
    patch.width = static_cast<std::uint32_t>(r.maxX - r.minX + 1) * scale;
    patch.height = static_cast<std::uint32_t>(r.maxZ - r.minZ + 1) * scale;
    patch.rgba.resize(static_cast<std::size_t>(patch.width) * patch.height * 4);
    for (std::uint32_t py = 0; py < patch.height; ++py)
        for (std::uint32_t px = 0; px < patch.width; ++px)
        {
            const auto type = world.GetTile(r.minX + static_cast<int>(px / scale), r.minZ + static_cast<int>(py / scale)).zoneType;
            const auto color = Zone::GetZoneDefinition(type).color;
            const float rgb[] = {color.r, color.g, color.b};
            const auto source = ((static_cast<std::size_t>(r.minZ) * scale + py) * base.width + r.minX * scale + px) * 4;
            const auto destination = (static_cast<std::size_t>(py) * patch.width + px) * 4;
            for (int channel = 0; channel < 3; ++channel)
                patch.rgba[destination + channel] = type == ZoneType::None ? base.rgba[source + channel] :
                    Byte(std::lerp(base.rgba[source + channel] / 255.0f, rgb[channel], ZoneOverlayOpacity));
            patch.rgba[destination + 3] = 255;
        }
    return patch;
}
