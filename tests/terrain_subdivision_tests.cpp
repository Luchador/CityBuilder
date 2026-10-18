#include "terrain.hpp"
#include "terrain_skirt.hpp"
#include "water.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 0.00005f)
    {
        return std::abs(a - b) <= tolerance;
    }

    float OriginalHeight(const TerrainHeightmap& map, float x, float z)
    {
        const auto tx = std::min(static_cast<std::uint32_t>(x), MapSize - 1);
        const auto tz = std::min(static_cast<std::uint32_t>(z), MapSize - 1);
        const float u = x - static_cast<float>(tx), v = z - static_cast<float>(tz);
        if (u + v <= 1)
            return (1 - u - v) * map.Height(tx, tz) + u * map.Height(tx + 1, tz) + v * map.Height(tx, tz + 1);
        return (u + v - 1) * map.Height(tx + 1, tz + 1) + (1 - v) * map.Height(tx + 1, tz) + (1 - u) * map.Height(tx, tz + 1);
    }

    void GeometryAndJoins()
    {
        TerrainHeightmap map;
        // Many crests, troughs, saddles, and mixed wet/dry tiles, including edges.
        for (std::uint32_t z = 0; z <= MapSize; ++z)
            for (std::uint32_t x = 0; x <= MapSize; ++x)
                map.heights[z * TerrainStride + x] = 0.25f * (static_cast<int>((x * 7 + z * 11) % 17) - 8);
        const auto savedHeights = map.heights;
        const auto mesh = BuildTerrain(map);
        Check(map.heights == savedHeights, "Rendering changed the logical terrain");
        Check(mesh.vertices.size() == 769 * 769, "Shared vertex count is wrong");
        Check(mesh.indices.size() == MapSize * MapSize * 18 * 3, "Each tile needs eighteen triangles");
        Check(mesh.lines.size() == 4 * MapSize * TerrainStride, "G grid must still mark logical tiles");

        for (const auto& v : mesh.vertices)
        {
            Check(Near(v.y, OriginalHeight(map, v.x, v.z)), "Subdivision changed a ground vertex height");
            Check(Near(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz, 1), "Normal is not unit length");
            Check(v.ny > 0, "Normal must face upward");
        }
        for (const auto& v : mesh.lines)
        {
            Check(v.x == std::floor(v.x) && v.z == std::floor(v.z), "Grid includes internal subdivisions");
            Check(v.y == map.Height(static_cast<std::uint32_t>(v.x), static_cast<std::uint32_t>(v.z)), "Grid does not meet terrain");
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const auto& a = mesh.vertices.at(mesh.indices[i]);
            const auto& b = mesh.vertices.at(mesh.indices[i + 1]);
            const auto& c = mesh.vertices.at(mesh.indices[i + 2]);
            const float normalY = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
            Check(normalY > 0, "Triangle is degenerate or faces downward");
            // Checking inside triangles catches a diagonal that crosses the old
            // saddle surface even when all its vertices individually match.
            const float x = 0.25f * a.x + 0.5f * b.x + 0.25f * c.x;
            const float z = 0.25f * a.z + 0.5f * b.z + 0.25f * c.z;
            const float y = 0.25f * a.y + 0.5f * b.y + 0.25f * c.y;
            Check(Near(y, OriginalHeight(map, x, z), 0.00015f), "A subtriangle changed the original surface");
        }

        // Count index references along an internal shared edge. Both neighboring
        // tiles must reference the same vertex, not two coincident copies.
        const auto shared = (30 * TerrainMeshStride + 24) * TerrainSubdivisions + TerrainMeshStride;
        Check(std::count(mesh.indices.begin(), mesh.indices.end(), shared) == 6, "Shared edge vertex is disconnected");

        const auto skirt = BuildTerrainSkirt(map);
        for (std::size_t i = 0; i < skirt.vertices.size(); i += 2)
        {
            const auto& top = skirt.vertices[i];
            Check(top.y == OriginalHeight(map, top.x, top.z), "Skirt top does not meet terrain");
            Check(skirt.vertices[i + 1].y == -40, "Skirt bottom moved");
        }
        const auto water = BuildWater(map);
        for (const auto& v : water.vertices)
        {
            Check(v.y == TerrainSeaLevel, "Water surface moved");
            Check(v.waterDepth == TerrainSeaLevel - OriginalHeight(map, v.x, v.z), "Water depth does not match terrain");
        }
        // Existing sparse water geometry is sufficient: subdivision preserves
        // those same ground triangles, so their interpolated depths still match.
        for (std::size_t i = 0; i < water.indices.size(); i += 3)
        {
            const auto& a = water.vertices[water.indices[i]];
            const auto& b = water.vertices[water.indices[i + 1]];
            const auto& c = water.vertices[water.indices[i + 2]];
            const float x = 0.25f * a.x + 0.5f * b.x + 0.25f * c.x;
            const float z = 0.25f * a.z + 0.5f * b.z + 0.25f * c.z;
            Check(Near(0.25f * a.waterDepth + 0.5f * b.waterDepth + 0.25f * c.waterDepth,
                       TerrainSeaLevel - OriginalHeight(map, x, z)), "Water shoreline changed");
        }
    }

    void BorderShading()
    {
        TerrainHeightmap map;
        for (std::uint32_t z = 0; z <= MapSize; ++z)
            for (std::uint32_t x = 129; x <= MapSize; ++x)
                map.heights[z * TerrainStride + x] = 0.25f;
        const auto mesh = BuildTerrain(map);
        const auto at = [&](std::uint32_t sx, std::uint32_t sz) -> const Vertex&
        {
            return mesh.vertices[sz * TerrainMeshStride + sx];
        };
        // Flat tiles beside a single rising tile. The large center should be
        // consistently lit; the shared edge takes the halfway slope.
        for (const auto z : {0u, 100u, 101u, 102u, TerrainMeshSize})
        {
            Check(at(127 * 3 + 2, z).nx == 0, "Flat center should remain flat-lit");
            for (const auto x : {128 * 3 + 1, 128 * 3 + 2})
                Check(Near(-at(x, z).nx / at(x, z).ny, 0.25f), "Tile center normal is not consistent");
            Check(Near(-at(128 * 3, z).nx / at(128 * 3, z).ny, 0.125f), "Shared border shading is not blended");
        }
        Check(Near(at(1, 1).x, TerrainTileBorder) && Near(at(2, 2).x, 1 - TerrainTileBorder), "Inset widths are wrong");
    }
}

int main()
{
    try
    {
        GeometryAndJoins();
        BorderShading();
        std::cout << "PASS: nine quads, shared vertices, winding, unchanged heights/shorelines/skirt/grid, border normals\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
