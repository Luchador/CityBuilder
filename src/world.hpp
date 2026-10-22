#pragma once

#include "map_dimensions.hpp"
#include "tile.hpp"
#include "road.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Allowed elevation increments
constexpr float TerrainHeightStep = 0.25f;
constexpr float TerrainSeaLevel = 0.0f;
// Broad flat land sits above the water. Keep this aligned to TerrainHeightStep.
constexpr float TerrainBaseElevation = 0.25f;
constexpr float TerrainMaxElevation = 256.0f;

constexpr bool IsWaterElevation(float height)
{
    return height < TerrainSeaLevel;
}

struct LakeSettings
{
    std::uint32_t count = 6;    // Maximum lakes; fewer if there isn't enough flat land. Range: 0..64.
    float minRadius = 10.0f;    // Radius in tiles, before shoreline variation. Range: 4..64.
    float maxRadius = 22.0f;    // Range: minRadius..64.
    float depth = 3.0f;         // Maximum depth below sea level; snapped down to a height step. Range: 0..256.
};

struct TerrainSettings
{
    MapDimensions dimensions;
    std::uint32_t seed = 1337;
    float hillHeight = 6.0f;   // Rise above the base; step-snapped, with total elevation capped at 256. Range: 0..256.
    float hillSize = 32.0f;     // Broad noise wavelength in tiles. Range: 8..1024.
    float flatCoverage = 0.7f;  // Fraction flattened to TerrainBaseElevation before height snapping. Range: 0..1.
    LakeSettings lakes;         // Second pass; count=0 or depth=0 keeps the original landscape.
};

// One shared height per tile corner. World owns the live copy; mesh builders
// derive rendering data from it. Adjacent tiles share their corner heights.
struct TerrainHeightmap
{
    explicit TerrainHeightmap(MapDimensions mapDimensions = MapDimensions{}) : dimensions(mapDimensions), heights(dimensions.CornerCount(), 0.0f) {}

    MapDimensions dimensions;
    std::vector<float> heights;

    void Validate() const;

    // x and z must be in 0..dimensions.TileCount(), inclusive.
    float Height(std::uint32_t x, std::uint32_t z) const
    {
        return heights[z * dimensions.CornerStride() + x];
    }
};

/**
 * Persistent CPU world data. Construct when the Create City form is accepted.
 * The renderer owns meshes and GPU resources derived from this data.
 */
class World
{
public:
    explicit World(const TerrainSettings& settings);

    const MapDimensions& GetDimensions() const { return terrain.dimensions; }

    // Snapshot of the settings used to generate this world's initial terrain.
    const TerrainSettings& GetTerrainSettings() const { return terrainSettings; }
    const TerrainHeightmap& GetTerrain() const { return terrain; }
    std::span<const Tile> GetTiles() const { return tiles; }

    // Tile coordinates are 0..TileCount()-1. Heightmap corners also include
    // TileCount(). Signed coordinates let picking reject positions off the map.
    bool ContainsTile(int x, int z) const;
    Tile& GetTile(int x, int z);
    const Tile& GetTile(int x, int z) const;

    std::span<const Road> GetRoads() const { return roads; }
    RoadPlacementStatus CheckRoadPlacement(int x, int z) const;
    // Single-tile, X-axis straight roads for this first pass. Clears an empty
    // zone and reserves infrastructure occupancy. No construction cost yet.
    bool PlaceRoad(int x, int z);

private:
    std::size_t TileIndex(int x, int z) const;

    TerrainSettings terrainSettings;
    TerrainHeightmap terrain;
    std::vector<Tile> tiles; // Row-major: z * tileCount + x.
    std::vector<Road> roads;
    InfrastructureId nextInfrastructureId = 1;
};
