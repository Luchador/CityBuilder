#include "world.hpp"
#include "road_footprint.hpp"
#include "road_mesh.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr int Center = 16;
    constexpr int DX[]{-1, 0, 1, 0, -1, 1, 1, -1}, DZ[]{0, -1, 0, 1, -1, -1, 1, 1};
    void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
    World FlatWorld()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(32); settings.hillHeight = 0; settings.lakes.count = 0;
        return World(settings);
    }
    RoadPath Arm(int direction, int from = 0, int to = 4)
    {
        return {Center + from * DX[direction], Center + from * DZ[direction],
                Center + to * DX[direction], Center + to * DZ[direction], true, direction >= 4};
    }
    void CheckNetwork(const World& world)
    {
        for (const auto& a : world.GetRoads())
        {
            Check(!a.mixedConnections || (a.connections == a.mixedConnections && SupportedMixedConnections(a.connections)),
                  "Incomplete/unsupported mixed junction");
            Check(!a.diagonalAxis || SupportedDiagonalConnections(a.connections), "Unsupported diagonal bend");
            for (int d = 0; d < 8; ++d) if (a.connections & (1 << d))
            {
                const auto* b = world.GetRoadAt(a.tileX + DX[d], a.tileZ + DZ[d]);
                Check(b && (b->connections & RotateRoadConnections(static_cast<std::uint8_t>(1 << d), 2)),
                      "One-way geometric connection");
            }
            for (const auto& b : world.GetRoads()) if (a.id < b.id)
                Check(!RoadMeshesOverlap(a, b), "Pavement overlap after commit");
        }
    }
    void CheckAllJunctions()
    {
        for (const auto& asset : RoadAssets) if (asset.mixedJunction)
            for (unsigned turn = 0; turn < 4; ++turn)
                for (bool branchFirst : {false, true}) for (bool reverse : {false, true})
                {
                    const auto mask = RotateRoadConnections(asset.connections, turn);
                    const auto label = std::string(asset.model) + " rotation=" + std::to_string(turn) +
                        " branchFirst=" + std::to_string(branchFirst) + " reverse=" + std::to_string(reverse);
                    auto world = FlatWorld();
                    int through = -1;
                    for (int d = 0; d < 8; ++d)
                        if ((mask & (1 << d)) && (mask & RotateRoadConnections(static_cast<std::uint8_t>(1 << d), 2)))
                        { through = d; break; }
                    Check(through >= 0, "Junction has no through route");
                    const auto throughMask = static_cast<std::uint8_t>((1 << through) |
                        RotateRoadConnections(static_cast<std::uint8_t>(1 << through), 2));
                    const auto add = [&](RoadPath path)
                    {
                        if (reverse) { std::swap(path.startX, path.endX); std::swap(path.startZ, path.endZ); }
                        const auto plan = world.PlanRoads(path);
                        Check(plan.evaluation.valid && plan.evaluation.blocked == 0, "Rejected fixture: " + label);
                        const auto result = world.PlaceRoads(path);
                        Check(result.changed == plan.evaluation.changes, "Plan/commit mismatch: " + label);
                        CheckNetwork(world);
                    };
                    const auto branch = [&]
                    {
                        for (int d = 0; d < 8; ++d)
                            if ((mask & ~throughMask) & (1 << d)) add(Arm(d));
                    };
                    if (branchFirst) branch();
                    add(Arm(through, -4, 4));
                    const auto id = world.GetRoadAt(Center, Center)->id;
                    if (!branchFirst) branch();
                    const auto* center = world.GetRoadAt(Center, Center);
                    Check(center && center->id == id && center->connections == mask && center->mixedConnections == mask &&
                          !center->diagonalAxis, "Junction topology/identity mismatch: " + label);
                    const auto visual = GetRoadVisual(*center);
                    Check(IsMixedRoadJunction(mask) && visual.lengthScale == 1 &&
                          std::floor(visual.quarterTurns) == visual.quarterTurns &&
                          RotateRoadConnections(RoadAssets[static_cast<std::size_t>(visual.shape)].connections,
                                                static_cast<unsigned>(visual.quarterTurns)) == mask,
                          "Mixed mesh rotated/stretched incorrectly: " + label);
                    const auto revision = world.GetRoadRevision();
                    const auto reuse = world.PlaceRoads(Arm(through, -4, 4));
                    Check(!reuse.changed && !reuse.evaluation.blocked && revision == world.GetRoadRevision(),
                          "Reused junction charged/changed: " + label);
                }
        std::cout << "PASS: six meshes, all quarter rotations, both build/drag orders, stable IDs, reuse and collision-free topology\n";
    }
    void CheckBendUpgrades()
    {
        for (auto diagonal : {RoadNorthWest, RoadSouthWest}) for (unsigned turn = 0; turn < 4; ++turn)
        {
            auto world = FlatWorld();
            const auto rotateIndex = [turn](int d)
            { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d), turn))); };
            const int e = rotateIndex(2), w = rotateIndex(0), diag = rotateIndex(std::countr_zero(static_cast<unsigned>(diagonal)));
            Check(!world.PlaceRoads(Arm(e)).evaluation.blocked, "Bend grid approach failed");
            const auto id = world.GetRoadAt(Center, Center)->id;
            Check(world.PlaceRoads(Arm(diag)).evaluation.upgrades == 1, "Bend creation failed");
            Check(IsRoadTransition(world.GetRoadAt(Center, Center)->mixedConnections), "Missing transition");
            Check(world.PlaceRoads(Arm(w)).evaluation.upgrades == 1, "Bend to T failed");
            Check(std::popcount(world.GetRoadAt(Center, Center)->mixedConnections) == 3, "Missing T upgrade");
            const int opposite = (diag - 4 + 2) % 4 + 4;
            Check(world.PlaceRoads(Arm(opposite)).evaluation.upgrades == 1, "T to cross failed");
            Check(world.GetRoadAt(Center, Center)->id == id &&
                  std::popcount(world.GetRoadAt(Center, Center)->mixedConnections) == 4, "Missing cross upgrade");
            CheckNetwork(world);
            const auto before = *world.GetRoadAt(Center, Center);
            const auto fifth = world.PlaceRoads(Arm(rotateIndex(1)));
            Check(fifth.evaluation.blocked > 0 && world.GetRoadAt(Center, Center)->mixedConnections == before.mixedConnections,
                  "Unsupported fifth arm damaged junction");
            CheckNetwork(world);
        }
        std::cout << "PASS: mirrored/rotated bends upgrade to T then cross; fifth arms are rejected safely\n";
    }
    void CheckMissingCenters()
    {
        for (const auto& asset : RoadAssets) if (asset.mixedJunction) for (unsigned turn = 0; turn < 4; ++turn)
        {
            auto world = FlatWorld();
            const auto mask = RotateRoadConnections(asset.connections, turn);
            for (int d = 0; d < 8; ++d) if (mask & (1 << d))
                Check(!world.PlaceRoads(Arm(d, 1)).evaluation.blocked, "Missing-center approach fixture failed");
            Check(!world.GetRoadAt(Center, Center) && world.PlaceRoad(Center, Center), "Single-click junction gap fill failed");
            Check(world.GetRoadAt(Center, Center)->mixedConnections == mask, "Gap fill selected wrong junction");
            CheckNetwork(world);
        }
        // Flared caps are wider than straights. The short cardinal stub would
        // overlap the adjacent diagonal approach; a longer branch has room.
        auto shortBranch = FlatWorld();
        shortBranch.PlaceRoads(Arm(4, -4, 4));
        Check(shortBranch.PlaceRoads(Arm(2, 0, 1)).evaluation.blocked > 0,
              "Short flared cap overlapped the junction");
        Check(!shortBranch.PlaceRoads(Arm(2, 0, 3)).evaluation.blocked, "Longer branch was unnecessarily blocked");
        CheckNetwork(shortBranch);
        std::cout << "PASS: single-click missing-center junctions and flared-cap clearance\n";
    }
    void CheckObstacles()
    {
        for (int kind = 0; kind < 4; ++kind)
        {
            auto world = FlatWorld();
            world.PlaceRoads(Arm(0, -4, 4));
            const auto before = *world.GetRoadAt(Center, Center);
            if (kind == 0) world.GetTile(Center + 1, Center - 1).buildingId = 123;
            if (kind == 1) world.GetTile(Center + 1, Center - 1).infrastructureId = 123;
            if (kind >= 2)
            {
                auto& t = const_cast<TerrainHeightmap&>(world.GetTerrain());
                t.heights[(Center - 1) * t.dimensions.CornerStride() + Center] = kind == 2 ? -1.0f : 1.0f;
            }
            const auto result = world.PlaceRoads(Arm(5));
            Check(result.evaluation.blocked > 0 && world.GetRoadAt(Center, Center)->id == before.id &&
                  world.GetRoadAt(Center, Center)->connections == before.connections &&
                  !world.GetRoadAt(Center, Center)->mixedConnections, "Blocked junction changed existing road");
            CheckNetwork(world);
        }
        auto world = FlatWorld();
        world.PlaceRoads(Arm(4, -4, 4));
        const auto before = world.GetRoads().size();
        const auto result = world.PlaceRoads({12, 19, 20, 11, true, true}); // diagonals cross between anchors
        Check(result.evaluation.blocked > 0 && world.GetRoads().size() >= before, "Between-tile crossing accepted");
        CheckNetwork(world);
        std::cout << "PASS: buildings, other infrastructure, water, uneven shoulders and unsupported crossings\n";
    }
    void CheckGeometry(const std::filesystem::path& assets)
    {
        for (std::size_t shape = 0; shape < RoadAssets.size(); ++shape)
        {
            const auto& asset = RoadAssets[shape];
            const auto mesh = LoadRoadMesh(assets / asset.model, asset);
            const auto profile = RoadCollisionShapes[shape];
            Check(profile.size() == mesh.indices.size(), "Collision profile index count is stale");
            for (std::size_t i = 0; i < profile.size(); ++i)
            {
                const auto& v = mesh.vertices[mesh.indices[i]];
                Check(std::abs(profile[i].x - v.x) < .000001 && std::abs(profile[i].z - v.z) < .000001,
                      "Collision profile no longer matches fitted mesh; regenerate it");
            }
            if (!asset.mixedJunction) continue;
            for (const auto& v : mesh.vertices)
                Check(std::abs(v.x) + std::abs(v.z) <= 1.00001f && std::abs(v.y) < .00001f && v.ny > .999f,
                      "Mixed mesh escaped footprint or lost upward normals");
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                const auto& a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]], c = mesh.vertices[mesh.indices[i + 2]];
                Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) > 0, "Mixed triangle faces down");
            }
        }
        std::cout << "PASS: actual GLB loading, collision profiles, footprint bounds and backface winding\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: CityMixedRoadTests assets-directory");
        CheckGeometry(argv[1]); CheckAllJunctions(); CheckBendUpgrades(); CheckMissingCenters(); CheckObstacles();
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
