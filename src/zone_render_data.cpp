#include "zone_render_data.hpp"
#include "world_zoning.hpp"
#include "world.hpp"

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
