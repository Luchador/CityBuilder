#include "camera.hpp"
#include "city.hpp"
#include "minimap_map.hpp"
#include "terrain_skirt.hpp"
#include "water.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template<class Action>
    void Reject(Action action)
    {
        try { action(); }
        catch (const std::invalid_argument&) { return; }
        throw std::runtime_error("Invalid map data was accepted");
    }

    void CheckSize(CityMapSize choice, std::uint32_t expected)
    {
        CityCreationOptions options;
        options.mapSize = choice;
        City city(options);
        Check(GetMapTileCount(city.mapSize) == expected, "Wrong city map size");

        TerrainSettings settings;
        settings.dimensions = MapDimensions(GetMapTileCount(city.mapSize));
        const auto n = expected;
        const auto stride = n + 1;
        World world(settings);
        const auto& heights = world.GetTerrain();
        Check(world.GetDimensions().TileCount() == n, "World lost its map size");
        Check(world.GetTiles().size() == static_cast<std::size_t>(n) * n, "Wrong world tile count");
        Check(std::all_of(world.GetTiles().begin(), world.GetTiles().end(),
                         [](const Tile& tile) { return tile.zoneType == ZoneType::None; }),
              "New world tiles must be unzoned");
        Check(world.ContainsTile(0, 0) && world.ContainsTile(n - 1, n - 1), "Missing world edge tiles");
        Check(!world.ContainsTile(-1, 0) && !world.ContainsTile(0, -1) &&
              !world.ContainsTile(n, 0) && !world.ContainsTile(0, n), "World accepts off-map tiles");
        world.GetTile(n - 1, n - 1).zoneType = ZoneType::ResidentialLight;
        const World& readOnlyWorld = world;
        Check(readOnlyWorld.GetTile(n - 1, n - 1).zoneType == ZoneType::ResidentialLight &&
              world.GetTiles().back().zoneType == ZoneType::ResidentialLight &&
              world.GetTile(0, n - 1).zoneType == ZoneType::None, "Wrong tile row stride");
        heights.Validate();
        Check(heights.heights.size() == stride * stride, "Wrong heightmap size");
        Check(std::any_of(heights.heights.begin(), heights.heights.end(), IsWaterElevation), "Seed must produce lakes");
        for (float height : heights.heights)
        {
            Check(std::isfinite(height) && height <= TerrainMaxElevation, "Invalid elevation");
            Check(height / TerrainHeightStep == std::round(height / TerrainHeightStep), "Unsnapped elevation");
        }

        const auto mesh = BuildTerrain(heights);
        Check(mesh.dimensions.TileCount() == n, "Mesh lost its dimensions");
        Check(mesh.vertices.size() == stride * stride, "Wrong terrain vertex count");
        Check(mesh.indices.size() == 6 * n * n, "Wrong terrain triangle count");
        Check(mesh.lineIndices.size() == 4 * n * stride, "Wrong grid line count");
        for (std::uint32_t z = 0; z <= n; ++z)
        {
            for (std::uint32_t x = 0; x <= n; ++x)
            {
                const auto& v = mesh.vertices[z * stride + x];
                Check(v.x == x && v.z == z && v.y == heights.Height(x, z), "Wrong shared corner");
                Check(std::abs(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz - 1.0f) < 0.00001f,
                      "Invalid terrain normal");
            }
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            Check(mesh.indices[i] < mesh.vertices.size() && mesh.indices[i + 1] < mesh.vertices.size() &&
                  mesh.indices[i + 2] < mesh.vertices.size(), "Terrain index out of bounds");
            const auto& a = mesh.vertices[mesh.indices[i]];
            const auto& b = mesh.vertices[mesh.indices[i + 1]];
            const auto& c = mesh.vertices[mesh.indices[i + 2]];
            Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) == 1.0f,
                  "Terrain triangle has incorrect area or winding");
        }

        // Count every unique horizontal/vertical edge, including all four borders.
        std::vector<unsigned char> edgeVisits(2 * n * stride, 0);
        for (std::size_t i = 0; i < mesh.lineIndices.size(); i += 2)
        {
            const auto a = mesh.lineIndices[i], b = mesh.lineIndices[i + 1];
            Check(a < mesh.vertices.size() && b < mesh.vertices.size(), "Grid index out of bounds");
            const auto x = a % stride, z = a / stride;
            std::size_t edge = 0;
            if (b == a + 1 && x < n) edge = z * n + x;
            else
            {
                Check(b == a + stride && z < n, "Grid segment crosses a row or skips a tile");
                edge = n * stride + x * n + z;
            }
            Check(++edgeVisits[edge] == 1, "Duplicate grid edge");
        }
        Check(std::all_of(edgeVisits.begin(), edgeVisits.end(), [](auto visits) { return visits == 1; }),
              "Missing grid edge");

        const auto water = BuildWater(heights);
        std::size_t wetTiles = 0;
        for (std::uint32_t z = 0; z < n; ++z)
            for (std::uint32_t x = 0; x < n; ++x)
                if (IsWaterElevation(heights.Height(x, z)) || IsWaterElevation(heights.Height(x + 1, z)) ||
                    IsWaterElevation(heights.Height(x, z + 1)) || IsWaterElevation(heights.Height(x + 1, z + 1)))
                    ++wetTiles;
        Check(water.indices.size() == wetTiles * 6, "Water tile count differs from terrain");
        for (auto index : water.indices) Check(index < water.vertices.size(), "Water index out of bounds");
        for (const auto& v : water.vertices)
            Check(v.x >= 0 && v.x <= n && v.z >= 0 && v.z <= n && v.y == TerrainSeaLevel, "Invalid water bounds");

        const auto skirt = BuildTerrainSkirt(heights);
        Check(skirt.vertices.size() == 8 * stride && skirt.indices.size() == 24 * n, "Incorrect skirt extent");
        for (std::size_t i = 0; i < skirt.vertices.size(); i += 2)
        {
            const auto& top = skirt.vertices[i];
            const auto& bottom = skirt.vertices[i + 1];
            Check(top.x == 0 || top.x == n || top.z == 0 || top.z == n, "Skirt is not on the map boundary");
            Check(top.y == heights.Height(static_cast<std::uint32_t>(top.x), static_cast<std::uint32_t>(top.z)),
                  "Skirt does not meet terrain");
            Check(bottom.x == top.x && bottom.z == top.z && bottom.y == TerrainSkirtSettings{}.bottomY,
                  "Skirt bottom is incorrect");
        }
        for (auto index : skirt.indices) Check(index < skirt.vertices.size(), "Skirt index out of bounds");

        const auto image = BuildMinimapImage(mesh);
        Check(image.width == n * 2 && image.height == n * 2 && image.rgba.size() == image.width * image.height * 4,
              "Incorrect minimap dimensions");
        for (std::size_t i = 3; i < image.rgba.size(); i += 4) Check(image.rgba[i] == 255, "Nonopaque minimap");

        Camera camera(heights.dimensions.Extent());
        Check(camera.GetCenterX() == n * 0.5f && camera.GetCenterZ() == n * 0.5f, "Camera not centered");
        camera.SetPosition(-10, static_cast<float>(n) + 10);
        Check(camera.GetCenterX() == 0 && camera.GetCenterZ() == n && camera.GetMapExtent() == n, "Incorrect camera bounds");
        for (int i = 0; i < 10; ++i) camera.Zoom(-10);
        Check(camera.GetHalfHeight() == std::max(200.0f, n * (200.0f / 256.0f)), "Incorrect maximum zoom");
        camera.ResetZoom();
        Check(camera.GetHalfHeight() == 120, "Starting zoom changed");

        std::cout << "PASS: " << n << 'x' << n << ", " << mesh.indices.size() / 3
                  << " terrain triangles, " << mesh.lineIndices.size() / 2 << " grid segments\n";
    }

    void CheckFarCornerAndValidation()
    {
        // A basin in the final tile catches stale 256-tile stride/loop bounds.
        TerrainHeightmap heights(MapDimensions{1024});
        std::fill(heights.heights.begin(), heights.heights.end(), TerrainBaseElevation);
        const auto stride = heights.dimensions.CornerStride();
        for (std::uint32_t z : {1023u, 1024u})
            for (std::uint32_t x : {1023u, 1024u}) heights.heights[z * stride + x] = -1;
        const auto water = BuildWater(heights);
        Check(water.indices.size() == 4 * 6, "Far-corner water missing or covers extra tiles");
        Check(std::any_of(water.vertices.begin(), water.vertices.end(), [](const auto& v)
              { return v.x == 1024 && v.z == 1024; }), "Water never reaches the final corner");

        auto mesh = BuildTerrain(heights);
        std::vector<MinimapTileOverlay> overlays(1024 * 1024);
        overlays[1024 * 1024 - 3] = {{1, 0, 0}, 1}; // Dry tile next to the far-corner basin.
        const auto image = BuildMinimapImage(mesh, overlays);
        const auto last = image.rgba.size() - 4;
        Check(image.rgba[last] == std::lround(WaterColor.r * 255) &&
              image.rgba[last + 1] == std::lround(WaterColor.g * 255) &&
              image.rgba[last + 2] == std::lround(WaterColor.b * 255), "Far-corner lake absent from minimap");
        const auto overlayPixel = ((image.height - 1) * image.width + (1021 * 2)) * 4;
        Check(image.rgba[overlayPixel] == 255 && image.rgba[overlayPixel + 1] == 0 && image.rgba[overlayPixel + 2] == 0,
              "Minimap overlay uses the wrong row stride");

        // Constructing another map must not change an existing map's indexing.
        TerrainHeightmap small(MapDimensions{128});
        Check(heights.Height(1024, 1024) == -1 && small.Height(128, 128) == 0, "Maps share mutable dimensions");
        heights.heights.pop_back();
        Reject([&] { heights.Validate(); });
        mesh.dimensions = MapDimensions{128};
        Reject([&] { BuildMinimapImage(mesh); });
        Reject([] { MapDimensions invalid(0); });
        Reject([] { MapDimensions invalid(std::numeric_limits<std::uint32_t>::max()); });
        Reject([] { GetMapTileCount(static_cast<CityMapSize>(99)); });
    }

    void CheckWorldOwnership()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions{8};
        settings.seed = 12345;
        settings.hillHeight = 2.0f;
        settings.lakes.count = 0;
        const auto expected = GenerateTerrain(settings);
        World first(settings);
        Check(first.GetTerrain().heights == expected.heights, "World changed terrain generation");
        settings.dimensions = MapDimensions{2};
        settings.seed = 98765;
        World second(settings);
        Check(first.GetDimensions().TileCount() == 8 && first.GetTerrainSettings().seed == 12345 &&
              first.GetTerrainSettings().hillHeight == 2.0f && second.GetDimensions().TileCount() == 2,
              "World settings are not independently owned");
        first.GetTile(1, 1).zoneType = ZoneType::CommercialLight;
        Check(second.GetTile(1, 1).zoneType == ZoneType::None, "Worlds share tile storage");
        for (const auto& coordinates : {std::pair{-1, 0}, std::pair{0, -1}, std::pair{8, 0}, std::pair{0, 8}})
        {
            bool rejected = false;
            try { first.GetTile(coordinates.first, coordinates.second); }
            catch (const std::out_of_range&) { rejected = true; }
            Check(rejected, "Mutable tile lookup must reject invalid coordinates");
            rejected = false;
            const World& readOnly = first;
            try { readOnly.GetTile(coordinates.first, coordinates.second); }
            catch (const std::out_of_range&) { rejected = true; }
            Check(rejected, "Const tile lookup must reject invalid coordinates");
        }
        settings.hillSize = 0;
        Reject([&] { World invalid(settings); });
    }
}

int main()
{
    try
    {
        CheckSize(CityMapSize::Small, 128);
        CheckSize(CityMapSize::Medium, 256);
        CheckSize(CityMapSize::Large, 512);
        CheckSize(CityMapSize::Giant, 1024);
        CheckFarCornerAndValidation();
        CheckWorldOwnership();
        std::cout << "PASS: far-corner water/overlays, world ownership, tile bounds, invalid dimensions\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
