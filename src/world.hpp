#pragma once

#include "map_dimensions.hpp"
#include "tile.hpp"
#include "road.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
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
    const Road* GetRoadAt(int x, int z) const;
    // Resolve any tile of a multi-cell module to the road owning its model.
    // GetRoadAt still returns the per-tile node used by the road network.
    const Road* GetRoadModuleAt(int x, int z) const;
    std::uint64_t GetRoadRevision() const { return roadRevision; }
    // 0 means a single click; dragged routes specify their needed directions.
    // Sloped cardinal roads require level pairs; diagonal roads require their
    // entire footprint to be flat, dry and at the same elevation (five tiles
    // for diagonals, three for grid/diagonal bends).
    RoadPlacementStatus CheckRoadPlacement(int x, int z, std::uint8_t requiredConnections = 0) const;
    // 0 = blocked, otherwise the directions a road can use on this tile.
    std::uint8_t GetRoadAllowedConnections(int x, int z) const;
    RoadPlan PlanRoads(const RoadPath& path) const;
    RoadEvaluation EvaluateRoads(const RoadPath& path) const;
    RoadPlacementResult PlaceRoads(const RoadPath& path);
    // Clears zones in the footprint, reserves occupancy and reconnects neighbors.
    // May upgrade an existing diagonal junction without changing its ID.
    // No construction cost yet.
    bool PlaceRoad(int x, int z, std::uint8_t requiredConnections = 0);

private:
    std::size_t TileIndex(int x, int z) const;
    void ReconnectRoad(int x, int z);
    RoadPlan PlanRoadsInternal(const RoadPath& path, std::uint8_t singleConnections) const;
    RoadPlacementResult CommitRoadPlan(const RoadPlan& plan);
    std::optional<Road> PlannedRoadAt(int x, int z, const RoadPath* path = nullptr, const RoadPlan* plan = nullptr) const;
    std::optional<Road> RawPlannedRoadAt(int x, int z, const RoadPath* path, const RoadPlan* plan) const;
    RoadPlacementStatus CheckRoadFootprint(const Road& road, const RoadPath& path, const RoadPlan& plan) const;
    std::uint8_t RoadConnectionsAt(const Road& road, const RoadPath* path = nullptr, const RoadPlan* plan = nullptr) const;
    void InsertRoad(const PlannedRoadTile& tile);
    void RefreshRoadReservations(int x, int z);
    void ReconnectRoadNeighborhood(int x, int z);
    void RefreshRoadModules(int x, int z);

    TerrainSettings terrainSettings;
    TerrainHeightmap terrain;
    std::vector<Tile> tiles; // Row-major: z * tileCount + x.
    std::vector<Road> roads;
    // 0 = no road, otherwise index + 1. Constant-time neighbor lookup, even
    // on giant maps; infrastructure IDs remain independent of vector indices.
    std::vector<std::uint32_t> roadAtTile;
    // One owner of each reserved terrain tile (index + 1). Several non-overlapping
    // road cells can share that tile. RefreshRoadReservations transfers ownership
    // when a transition shrinks an existing diagonal footprint.
    std::vector<std::uint32_t> roadFootprintAtTile;
    std::uint64_t roadRevision = 0;
    InfrastructureId nextInfrastructureId = 1;
};
