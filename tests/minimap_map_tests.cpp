#include "minimap_map.hpp"
#include "water.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template<class Action> void MustReject(Action action)
    {
        try { action(); }
        catch (const std::invalid_argument&) { return; }
        throw std::runtime_error("Invalid minimap input was accepted");
    }

    void CheckPixel(const MinimapImage& image, std::uint32_t x, std::uint32_t y, TerrainColor color)
    {
        const auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
        const std::array<float, 3> expected{color.r, color.g, color.b};
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            Check(std::abs(static_cast<int>(image.rgba[i + channel]) - std::lround(expected[channel] * 255)) <= 1,
                  "Minimap pixel did not match the world");
        }
        Check(image.rgba[i + 3] == 255, "Minimap must be opaque");
    }

    TerrainMesh Flat(float height)
    {
        TerrainHeightmap map;
        std::fill(map.heights.begin(), map.heights.end(), height);
        return BuildTerrain(map);
    }

    TerrainColor WaterOverRed(float coverage)
    {
        return {1.0f - coverage + coverage * WaterColor.r,
                coverage * WaterColor.g, coverage * WaterColor.b};
    }

    void FlatMaps()
    {
        for (float height : {-1.0f, 0.0f, 1.0f, 256.0f})
        {
            const auto image = BuildMinimapImage(Flat(height));
            Check(image.width == 512 && image.height == 512 && image.rgba.size() == 512 * 512 * 4,
                  "Unexpected minimap image dimensions");
            const auto color = IsWaterElevation(height) ? WaterColor : SampleTerrainColor(height);
            for (std::uint32_t y = 0; y < image.height; ++y)
                for (std::uint32_t x = 0; x < image.width; ++x) CheckPixel(image, x, y, color);
        }
    }

    void PaletteAndOrientation()
    {
        auto mesh = Flat(1.0f);
        // A known linear vertex-color field catches transposition, mirroring,
        // border stretching, and accidental resampling of a default palette.
        for (auto& v : mesh.vertices)
        {
            v.r = v.x / MapExtent;
            v.g = v.z / MapExtent;
            v.b = 0.2f;
        }
        const auto image = BuildMinimapImage(mesh);
        for (const std::uint32_t y : {0u, 81u, 256u, 511u})
            for (const std::uint32_t x : {0u, 37u, 256u, 511u})
                CheckPixel(image, x, y, {(x + 0.5f) / image.width, (y + 0.5f) / image.height, 0.2f});

        TerrainPalette palette;
        for (auto& band : palette.bands) band.color = {0.8f, 0.1f, 0.6f};
        const auto custom = BuildMinimapImage(BuildTerrain(TerrainHeightmap{}, palette));
        CheckPixel(custom, 256, 256, {0.8f, 0.1f, 0.6f});
    }

    void ShorelinesAndOverlays()
    {
        TerrainHeightmap map;
        std::fill(map.heights.begin(), map.heights.end(), 1.0f);
        const auto setTile = [&](std::uint32_t x, std::uint32_t z, float a, float b, float c, float d)
        {
            map.heights[z * TerrainStride + x] = a;
            map.heights[z * TerrainStride + x + 1] = b;
            map.heights[(z + 1) * TerrainStride + x] = c;
            map.heights[(z + 1) * TerrainStride + x + 1] = d;
        };
        setTile(10, 20, -1, 1, 1, -1); // Saddle: bilinear sampling would be wrong.
        setTile(30, 40, -1, 0, 0, 0);  // One wet triangle beside a dry sea-level one.
        auto mesh = BuildTerrain(map);
        for (auto& v : mesh.vertices) { v.r = 1; v.g = v.b = 0; }
        const auto image = BuildMinimapImage(mesh);
        CheckPixel(image, 20, 40, WaterOverRed(0.25f));
        CheckPixel(image, 21, 40, WaterOverRed(0.0f));
        CheckPixel(image, 20, 41, WaterOverRed(0.0f));
        CheckPixel(image, 21, 41, WaterOverRed(0.25f));
        CheckPixel(image, 60, 80, WaterColor);
        CheckPixel(image, 61, 80, WaterOverRed(0.25f));
        CheckPixel(image, 60, 81, WaterOverRed(0.25f));
        CheckPixel(image, 61, 81, {1, 0, 0});

        std::vector<MinimapTileOverlay> overlay(MapSize * MapSize);
        Check(BuildMinimapImage(mesh, overlay).rgba == image.rgba, "Transparent overlay altered terrain");
        overlay[60 * MapSize + 50] = {{0, 1, 0}, 0.5f};
        overlay[40 * MapSize + 30] = {{0, 0, 1}, 1};
        const auto zoned = BuildMinimapImage(mesh, overlay);
        CheckPixel(zoned, 100, 120, {0.5f, 0.5f, 0});
        CheckPixel(zoned, 102, 120, {1, 0, 0});
        CheckPixel(zoned, 60, 80, WaterColor); // Zones cannot hide water.
        CheckPixel(zoned, 61, 81, {0, 0, 1});
    }

    void InsetVertexColors()
    {
        auto mesh = Flat(1);
        // The logical corners are blue, but the large inset face is red. A
        // sampler still reading the old 257x257 grid would miss the new colors.
        for (auto& vertex : mesh.vertices) { vertex.r = vertex.g = 0; vertex.b = 1; }
        for (std::uint32_t z = 1; z <= 2; ++z)
            for (std::uint32_t x = 1; x <= 2; ++x)
            {
                auto& vertex = mesh.vertices[(42 * TerrainSubdivisions + z) * TerrainMeshStride + 71 * TerrainSubdivisions + x];
                vertex.r = 1;
                vertex.b = 0;
            }
        const auto image = BuildMinimapImage(mesh);
        // With the default 10% border, these four pixel samples lie in the inset.
        if (TerrainTileBorder <= 0.125f)
            CheckPixel(image, 142, 84, {1, 0, 0});
        CheckPixel(image, 144, 84, {0, 0, 1});
    }

    void ValidationAndRepeatability()
    {
        auto mesh = Flat(1);
        std::vector<MinimapTileOverlay> badSize(1);
        MustReject([&] { BuildMinimapImage(mesh, badSize); });
        std::vector<MinimapTileOverlay> invalid(MapSize * MapSize);
        invalid[0].opacity = std::numeric_limits<float>::quiet_NaN();
        MustReject([&] { BuildMinimapImage(mesh, invalid); });
        mesh.vertices[0].y = std::numeric_limits<float>::infinity();
        MustReject([&] { BuildMinimapImage(mesh); });
        mesh.vertices.pop_back();
        MustReject([&] { BuildMinimapImage(mesh); });

        const auto world = BuildTerrain(GenerateTerrain({}));
        const auto a = BuildMinimapImage(world);
        const auto b = BuildMinimapImage(world);
        Check(a.rgba == b.rgba, "Same terrain must produce the same minimap");
    }
}

int main()
{
    try
    {
        FlatMaps();
        PaletteAndOrientation();
        ShorelinesAndOverlays();
        InsetVertexColors();
        ValidationAndRepeatability();
        std::cout << "PASS: elevation colors, exact sea level, triangular shorelines, map axes, overlays, validation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
