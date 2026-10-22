#pragma once

#include <cstdint>
#include <stdexcept>

// One world coordinate unit is one tile. Apply this conversion uniformly to
// imported model X/Y/Z; a 16-meter road segment is one game unit long.
inline constexpr float MetersPerTile = 16.0f;

// Square maps, measured in tiles. Keep dimensions with each map/mesh rather
// than in mutable globals, so generation and rendering agree on the stride.
class MapDimensions
{
public:
    static constexpr std::uint32_t DefaultTileCount = 256;
    static constexpr std::uint32_t MaxTileCount = 1024;

    explicit MapDimensions(std::uint32_t tiles = DefaultTileCount) : tileCount(tiles)
    {
        if (tiles == 0 || tiles > MaxTileCount)
        {
            throw std::invalid_argument("Map dimensions must be between 1 and 1024 tiles per side.");
        }
    }

    std::uint32_t TileCount() const { return tileCount; }
    std::uint32_t CornerStride() const { return tileCount + 1; }
    std::uint32_t CornerCount() const { return CornerStride() * CornerStride(); }
    float Extent() const { return static_cast<float>(tileCount); }

private:
    std::uint32_t tileCount;
};
