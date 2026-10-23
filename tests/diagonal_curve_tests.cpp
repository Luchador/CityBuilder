#include "world.hpp"
#include "road_mesh.hpp"
#include "road_footprint.hpp"
#include "map_dimensions.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    constexpr int Center = 16;
    constexpr int DX[]{-1, 1, 1, -1}, DZ[]{-1, -1, 1, 1};
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    bool Near(float a, float b) { return std::abs(a - b) < .00001f; }
    World FlatWorld()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(32); settings.hillHeight = 0; settings.lakes.count = 0;
        return World(settings);
    }
    RoadPath Arm(int d, int from = 0, int to = 4)
    {
        return {Center + from * DX[d], Center + from * DZ[d],
                Center + to * DX[d], Center + to * DZ[d], true, true};
    }
    void CheckNetwork(const World& world)
    {
        for (const auto& a : world.GetRoads())
        {
            Check(a.diagonalAxis && !a.mixedConnections && SupportedDiagonalConnections(a.connections), "Lost diagonal topology");
            for (int d = 0; d < 4; ++d) if (a.connections & (16 << d))
            {
                const auto* b = world.GetRoadAt(a.tileX + DX[d], a.tileZ + DZ[d]);
                Check(b && (b->connections & (16 << ((d + 2) % 4))), "Connection is not reciprocal");
            }
            for (const auto& b : world.GetRoads()) if (a.id < b.id)
                Check(!RoadMeshesOverlap(a, b), "Curve or neighboring pavement overlaps");
        }
    }
    void CheckPlacement()
    {
        for (int turn = 0; turn < 4; ++turn) for (bool swap : {false, true}) for (bool reverse : {false, true})
        {
            auto world = FlatWorld();
            const int first = ((swap ? 2 : 1) + turn) % 4, second = ((swap ? 1 : 2) + turn) % 4;
            auto a = Arm(first), b = Arm(second);
            if (reverse) { std::swap(a.startX, a.endX); std::swap(a.startZ, a.endZ);
                           std::swap(b.startX, b.endX); std::swap(b.startZ, b.endZ); }
            Check(world.PlaceRoads(a).changed == 5, "First arm failed");
            const auto id = world.GetRoadAt(Center, Center)->id;
            const auto plan = world.PlanRoads(b);
            Check(!plan.evaluation.blocked && plan.evaluation.changes == 5 && plan.evaluation.upgrades == 1,
                  "Curve plan must add four tiles and upgrade the end");
            Check(world.PlaceRoads(b).changed == 5 && world.GetRoads().size() == 9, "Curve commit differs from preview");
            const auto* center = world.GetRoadAt(Center, Center);
            const auto mask = static_cast<std::uint8_t>((16 << first) | (16 << second));
            Check(center->id == id && center->connections == mask && center->diagonalAxis == RoadAllDiagonals &&
                  IsDiagonalRoadCurve(mask) && GetRoadVisual(*center).shape == RoadShape::DiagonalCurve,
                  "Curve lost its identity, ports or mesh");
            const auto revision = world.GetRoadRevision();
            Check(!world.PlaceRoads(a).changed && !world.PlaceRoads(b).changed && world.GetRoadRevision() == revision,
                  "Reusing a curve modified the world");
            CheckNetwork(world);
            Check(world.PlaceRoads(Arm((3 + turn) % 4)).evaluation.upgrades == 1 &&
                  GetRoadVisual(*world.GetRoadAt(Center, Center)).shape == RoadShape::DiagonalThreeWay,
                  "Curve did not upgrade to T");
            Check(world.PlaceRoads(Arm(turn)).evaluation.upgrades == 1 &&
                  GetRoadVisual(*world.GetRoadAt(Center, Center)).shape == RoadShape::DiagonalFourWay &&
                  world.GetRoadAt(Center, Center)->id == id, "Curve/T did not upgrade to cross");
            CheckNetwork(world);
        }
        for (int turn = 0; turn < 4; ++turn)
        {
            auto gap = FlatWorld();
            gap.PlaceRoads(Arm(turn, 1)); gap.PlaceRoads(Arm((turn + 1) % 4, 1));
            Check(gap.PlaceRoad(Center, Center) && GetRoadVisual(*gap.GetRoadAt(Center, Center)).shape == RoadShape::DiagonalCurve,
                  "Single-click missing-center curve failed");
            CheckNetwork(gap);
            // A neighboring addition can also change a curve's mesh without
            // including the curve itself in the drag; validate that update.
            Check(!gap.PlaceRoads(Arm((turn + 2) % 4, 1)).evaluation.blocked &&
                  GetRoadVisual(*gap.GetRoadAt(Center, Center)).shape == RoadShape::DiagonalThreeWay,
                  "Off-path curve did not reconnect as a T");
            CheckNetwork(gap);
        }
        std::cout << "PASS: all curve directions/build orders/drag directions, previews, stable IDs, reuse, gap fill and T/cross upgrades\n";
    }
    void CheckBlocked()
    {
        auto world = FlatWorld(); world.PlaceRoads(Arm(1));
        const auto before = *world.GetRoadAt(Center, Center);
        world.GetTile(Center + 1, Center + 1).buildingId = 42;
        const auto result = world.PlaceRoads(Arm(2));
        Check(result.evaluation.blocked > 0 && world.GetRoadAt(Center, Center)->id == before.id &&
              world.GetRoadAt(Center, Center)->connections == before.connections &&
              world.GetRoadAt(Center, Center)->diagonalAxis == before.diagonalAxis, "Blocked curve damaged existing end");
        CheckNetwork(world);
        for (const float height : {-1.0f, 1.0f})
        {
            auto uneven = FlatWorld();
            auto& t = const_cast<TerrainHeightmap&>(uneven.GetTerrain());
            t.heights[Center * t.dimensions.CornerStride() + Center] = height;
            Check(!RoadPlacementAccepted(uneven.CheckRoadPlacement(Center, Center, RoadNorthEast | RoadSouthEast)),
                  "Curve accepted water or uneven ground");
        }
        auto edge = FlatWorld();
        Check(!edge.PlaceRoad(0, 0, RoadNorthEast | RoadSouthEast), "Curve escaped map footprint");
        std::cout << "PASS: blocked approach preserves end; curves reject wet/uneven ground and off-map footprints\n";
    }
    void CheckMesh(const std::filesystem::path& assets)
    {
        const auto& asset = RoadAssets[static_cast<std::size_t>(RoadShape::DiagonalCurve)];
        const auto path = assets / asset.model;
        const auto source = LoadStaticGlb(path, 1.0f / MetersPerTile), mesh = LoadRoadMesh(path, asset);
        Check(source.hasTexcoords && mesh.indices == source.indices && mesh.vertices.size() == source.vertices.size(),
              "Authored curve topology/UVs were lost");
        for (std::size_t i = 0; i < source.vertices.size(); ++i)
        {
            const auto& a = source.vertices[i]; const auto& b = mesh.vertices[i];
            Check(a.x == b.x && a.y == b.y && a.z == b.z && Near(b.u, a.u * .5f + .25f) && b.v == a.v,
                  "Curve geometry was stretched or texture mapping was damaged");
            Check(Near(b.y, 0) && Near(b.ny, 1), "Curve is not flat/Y-up");
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const auto& a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]], c = mesh.vertices[mesh.indices[i + 2]];
            Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) > 0, "Curve triangle is back-facing");
        }
        const auto profile = RoadCollisionShapes[static_cast<std::size_t>(RoadShape::DiagonalCurve)];
        Check(profile.size() == mesh.indices.size(), "Curve collision profile has wrong triangle count");
        for (std::size_t i = 0; i < profile.size(); ++i)
        {
            const auto& v = mesh.vertices[mesh.indices[i]];
            Check(Near(static_cast<float>(profile[i].x), v.x) && Near(static_cast<float>(profile[i].z), v.z),
                  "Curve collision profile differs from its mesh");
        }
        for (unsigned turn = 0; turn < 4; ++turn)
        {
            const auto mask = RotateRoadConnections(asset.connections, turn);
            const auto visual = GetRoadVisual(mask);
            Check(visual.shape == RoadShape::DiagonalCurve && visual.quarterTurns == turn + .5f && visual.lengthScale == 1,
                  "Wrong curve yaw/scale");
            const float angle = visual.quarterTurns * 1.5707963267948966f;
            std::vector<std::pair<float, float>> positions;
            for (const auto& v : mesh.vertices)
            {
                const float x = v.x * std::cos(angle) - v.z * std::sin(angle), z = v.x * std::sin(angle) + v.z * std::cos(angle);
                Check(std::abs(x) + std::abs(z) <= 1.00001f, "Curve escaped its diagonal diamond");
                positions.emplace_back(x, z);
            }
            for (int d = 0; d < 4; ++d) if (mask & (16 << d)) for (float side : {-1.0f, 1.0f})
            {
                const float x = DX[d] * .5f - DZ[d] * side * .25f / RoadDiagonalLength;
                const float z = DZ[d] * .5f + DX[d] * side * .25f / RoadDiagonalLength;
                Check(std::any_of(positions.begin(), positions.end(), [&](auto p) { return Near(p.first, x) && Near(p.second, z); }),
                      "Eight-meter opening does not meet diagonal straight seam");
            }
        }
        std::cout << "PASS: authored wider-radius GLB, UV mapping, triangle winding, collision profile and all eight diagonal seams\n";
    }
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: CityDiagonalCurveTests assets-directory");
        CheckMesh(argv[1]); CheckPlacement(); CheckBlocked(); return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
