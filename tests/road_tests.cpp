#include "road_tool.hpp"
#include "static_mesh.hpp"
#include "toolcontroller.hpp"
#include "world.hpp"
#include "world_zoning.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
    bool Empty(const ToolResult& result) { return std::holds_alternative<std::monostate>(result); }
    World FlatWorld()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(16);
        settings.hillHeight = 0;
        settings.lakes.count = 0;
        return World(settings);
    }
    ToolPointer At(int x, int z)
    {
        return {100, 100, ToolHit{{x, z}, x + 0.5f, TerrainBaseElevation, z + 0.5f}};
    }
    template<class F> void Throws(F operation, const char* message)
    {
        bool threw = false;
        try { operation(); } catch (const std::exception&) { threw = true; }
        Check(threw, message);
    }

    void CheckWorldAndTool()
    {
        World world = FlatWorld();
        ToolController controller(world);
        controller.SetTool(std::make_unique<RoadTool>());
        Check(Empty(controller.Update(At(2, 3), .016f)) && Empty(controller.End(At(2, 3))),
              "Hover or flyout release placed a road");
        Check(controller.BuildPreview().tiles == ToolTileRect{2, 3, 2, 3} &&
              !controller.BuildPreview().zoneType && controller.BuildPreview().placementAllowed,
              "Valid road preview missing");
        world.GetTile(2, 3).zoneType = ZoneType::ResidentialLight;
        Check(controller.Begin(At(2, 3)), "Valid road click rejected");
        const auto request = std::get<RoadRequest>(controller.End(At(2, 3)));
        Check(request.tile == ToolTile{2, 3} && world.GetRoads().empty(), "Tool mutated world or chose wrong tile");
        Check(world.PlaceRoad(request.tile.x, request.tile.z), "Placement rejected");
        const auto& road = world.GetRoads().front();
        Check(road.tileX == 2 && road.tileZ == 3 && road.elevation == TerrainBaseElevation &&
              road.id != NoInfrastructure && world.GetTile(2, 3).infrastructureId == road.id &&
              world.GetTile(2, 3).zoneType == ZoneType::None, "Road record/occupancy/zone replacement disagree");
        Check(!world.PlaceRoad(2, 3) && world.GetRoads().size() == 1 && !CanZoneTile(world, 2, 3),
              "Duplicate road or zoning over road allowed");
        controller.Update(At(2, 3), .016f);
        Check(!controller.BuildPreview().placementAllowed && controller.Begin(At(2, 3)),
              "Invalid preview must still accept a gesture for rejection feedback");
        const auto blocked = std::get<RoadRequest>(controller.End(At(2, 3)));
        Check(!world.PlaceRoad(blocked.tile.x, blocked.tile.z), "Occupied road request was not rejected at commit");
        world.GetTile(4, 4).buildingId = 7;
        world.GetTile(5, 4).infrastructureId = 99;
        Check(!world.PlaceRoad(4, 4) && !world.PlaceRoad(5, 4), "Road replaced occupied tile");
        Check(!world.PlaceRoad(-1, 0) && !world.PlaceRoad(16, 0), "Off-map road placed");
        // Modify this test fixture only; live game terrain remains read-only.
        auto& terrain = const_cast<TerrainHeightmap&>(world.GetTerrain());
        terrain.heights[8 * terrain.dimensions.CornerStride() + 8] += TerrainHeightStep;
        Check(world.CheckRoadPlacement(7, 7) == RoadPlacementStatus::NotFlat && !world.PlaceRoad(7, 7),
              "A one-corner slope accepted a road");
        terrain.heights[10 * terrain.dimensions.CornerStride() + 10] = -TerrainHeightStep;
        Check(world.CheckRoadPlacement(9, 9) == RoadPlacementStatus::Underwater && !world.PlaceRoad(9, 9),
              "Partly submerged tile accepted a road");
        for (int z = 0; z <= 1; ++z) for (int x = 0; x <= 1; ++x)
            terrain.heights[z * terrain.dimensions.CornerStride() + x] = TerrainSeaLevel;
        Check(world.PlaceRoad(0, 0), "Dry, flat sea-level tile rejected");
        Check(world.GetRoads()[0].id != world.GetRoads()[1].id, "Infrastructure IDs repeated");
        controller.Begin(At(3, 3));
        controller.Update(At(3, 4), .016f);
        Check(!controller.BuildPreview().tiles && Empty(controller.End(At(3, 4))), "Drag created a road");
        controller.Begin(At(3, 3));
        controller.Cancel();
        Check(Empty(controller.End(At(3, 3))), "Canceled click created a road");
        controller.Begin(At(3, 3));
        Check(Empty(controller.End({})), "UI/off-map release used stale hover");
        controller.Begin(At(3, 3));
        world.GetTile(3, 3).buildingId = 1;
        const auto newlyBlocked = std::get<RoadRequest>(controller.End(At(3, 3)));
        Check(!world.PlaceRoad(newlyBlocked.tile.x, newlyBlocked.tile.z), "Commit ignored newly occupied tile");
        Check(world.PlaceRoad(15, 15), "Last map tile rejected");
        std::cout << "PASS: click/cancel, bounds, flat/dry rules, occupancy, IDs and zone replacement\n";
    }

    // Standalone transformed GLB fixture; no JSON library or GPU needed.
    void WriteFixture(const std::filesystem::path& path, bool badIndex = false, bool missingUv = false)
    {
        std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"translation":[16,32,48],"children":[1]},{"scale":[-1,2,3],"mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1)";
        if (!missingUv) json += R"(,"TEXCOORD_0":2)";
        json += R"(},"indices":3}]}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5125,"count":3,"type":"SCALAR"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":12}],
"buffers":[{"byteLength":108}]})";
        while (json.size() % 4) json += ' ';
        std::ofstream out(path, std::ios::binary);
        const auto u32 = [&](std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i) out.put(static_cast<char>((value >> (i * 8)) & 255));
        };
        u32(0x46546c67); u32(2); u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + 108));
        u32(static_cast<std::uint32_t>(json.size())); u32(0x4e4f534a);
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        u32(108); u32(0x004e4942);
        const float values[] = {0, 0, 0, 0, 0, 16, 16, 0, 0,
                                .70710678f, .70710678f, 0, .70710678f, .70710678f, 0, .70710678f, .70710678f, 0,
                                0, 0, 0, 1, 1, 0};
        for (float value : values) u32(std::bit_cast<std::uint32_t>(value));
        u32(0); u32(1); u32(badIndex ? 100 : 2);
        Check(static_cast<bool>(out), "Cannot write GLB test fixture");
    }

    void CheckImporter(const std::filesystem::path& suppliedRoad, const std::filesystem::path& scratch)
    {
        const auto mesh = LoadStaticGlb(suppliedRoad, 1.0f / MetersPerTile);
        Check(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "Supplied road geometry changed");
        float minX = 100, maxX = -100, minZ = 100, maxZ = -100;
        for (const auto& v : mesh.vertices)
        {
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
            Check(Near(v.y, 0) && Near(v.nx, 0) && Near(v.ny, 1) && Near(v.nz, 0), "Y up was not preserved");
            Check(Near(v.v, v.x + .5f) && Near(v.u, .5f - v.z), "Authored UVs/centerline were flipped");
        }
        Check(Near(minX, -.5f) && Near(maxX, .5f) && Near(minZ, -.25f) && Near(maxZ, .25f),
              "16 x 8 meter road was stretched or scaled incorrectly");
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const auto& a = mesh.vertices[mesh.indices[i]];
            const auto& b = mesh.vertices[mesh.indices[i + 1]];
            const auto& c = mesh.vertices[mesh.indices[i + 2]];
            Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) > 0,
                  "Road triangle winding is not upward-facing");
        }
        const auto fixture = scratch / "road-fixture.glb";
        WriteFixture(fixture);
        const auto transformed = LoadStaticGlb(fixture, 1.0f / MetersPerTile);
        const auto& v = transformed.vertices.front();
        Check(Near(v.x, 1) && Near(v.y, 2) && Near(v.z, 3) &&
              Near(transformed.vertices[1].z, 6) && Near(transformed.vertices[2].x, 0),
              "Parent/node transforms or meter conversion failed");
        Check(Near(v.nx, -2 / std::sqrt(5.0f)) && Near(v.ny, 1 / std::sqrt(5.0f)) && Near(v.nz, 0),
              "Nonuniform scale did not use inverse-transpose normals");
        Check(transformed.indices == std::vector<std::uint32_t>{0, 2, 1}, "Mirrored winding failed");
        WriteFixture(fixture, true);
        Throws([&] { LoadStaticGlb(fixture); }, "Out-of-range index accepted");
        WriteFixture(fixture, false, true);
        Throws([&] { LoadStaticGlb(fixture); }, "Missing UVs accepted");
        std::ofstream(fixture, std::ios::binary) << "not a GLB";
        Throws([&] { LoadStaticGlb(fixture); }, "Malformed GLB accepted");
        std::filesystem::remove(fixture);
        Throws([&] { LoadStaticGlb(fixture); }, "Missing model accepted");
        Throws([&] { LoadStaticGlb(suppliedRoad, std::numeric_limits<float>::quiet_NaN()); }, "Nonfinite scale accepted");
        std::cout << "PASS: real asset dimensions/normals/UVs/winding; transforms and invalid GLB handling\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: CityRoadTests road_straight.glb scratch-directory");
        CheckWorldAndTool();
        CheckImporter(argv[1], argv[2]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
