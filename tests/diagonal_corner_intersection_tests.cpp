#include "world.hpp"
#include "road_mesh.hpp"
#include "road_footprint.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr int Center = 16;
    constexpr int DX[]{-1,0,1,0,-1,1,1,-1}, DZ[]{0,-1,0,1,-1,-1,1,1};
    constexpr RoadShape Shapes[]{RoadShape::DiagonalCurveAcuteIntersection,
        RoadShape::DiagonalCurveReflexIntersection, RoadShape::DiagonalCurveDoubleIntersection};
    void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
    bool Near(float a, float b) { return std::abs(a-b) < .00001f; }
    World FlatWorld()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(32); settings.hillHeight = 0; settings.lakes.count = 0;
        return World(settings);
    }
    int Direction(int d, unsigned turn)
    { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d), turn))); }
    RoadPath Arm(int d, int from = 0, int to = 4, bool reverse = false)
    {
        RoadPath p{Center + from*DX[d], Center + from*DZ[d], Center + to*DX[d], Center + to*DZ[d], true, d >= 4};
        if (reverse) { std::swap(p.startX,p.endX); std::swap(p.startZ,p.endZ); }
        return p;
    }
    void CheckNetwork(const World& world)
    {
        for (const auto& a : world.GetRoads())
        {
            Check(!a.mixedConnections || (a.connections == a.mixedConnections && SupportedMixedConnections(a.connections)),
                  "Incomplete mixed junction");
            for (int d=0; d<8; ++d) if (a.connections & (1 << d))
            {
                const auto* b = world.GetRoadAt(a.tileX+DX[d],a.tileZ+DZ[d]);
                Check(b && (b->connections & RotateRoadConnections(static_cast<std::uint8_t>(1 << d),2)),
                      "Nonreciprocal connection");
            }
            for (const auto& b : world.GetRoads()) if (a.id < b.id)
                Check(!RoadMeshesOverlap(a,b), "Overlapping pavement");
        }
    }
    void Add(World& world, RoadPath path)
    {
        const auto plan = world.PlanRoads(path);
        Check(plan.evaluation.valid && !plan.evaluation.blocked,
              "Rejected arm (" + std::to_string(path.startX) + "," + std::to_string(path.startZ) + ")->(" +
              std::to_string(path.endX) + "," + std::to_string(path.endZ) + ")");
        Check(world.PlaceRoads(path).changed == plan.evaluation.changes, "Preview/commit mismatch");
        CheckNetwork(world);
    }
    void Expect(const World& world, RoadShape shape, unsigned turn)
    {
        const auto* center = world.GetRoadAt(Center,Center);
        const auto mask = RotateRoadConnections(RoadAssets[static_cast<std::size_t>(shape)].connections,turn);
        Check(center && center->connections == mask && center->mixedConnections == mask && !center->diagonalAxis,
              "Wrong junction ports: expected=" + std::to_string(mask) + " actual=" +
              std::to_string(center ? center->connections : 0) + " mixed=" +
              std::to_string(center ? center->mixedConnections : 0));
        const auto visual = GetRoadVisual(*center);
        Check(visual.shape == shape && visual.quarterTurns == turn && visual.lengthScale == 1, "Wrong junction mesh/transform");
    }
    void CheckPlacement()
    {
        for (unsigned turn=0; turn<4; ++turn) for (bool reverse : {false,true}) for (int side : {0,2})
        {
            auto world = FlatWorld();
            const int ne=Direction(5,turn), se=Direction(6,turn), branch=Direction(side,turn);
            Add(world,Arm(ne,0,4,reverse)); Add(world,Arm(se,0,4,reverse));
            const auto id = world.GetRoadAt(Center,Center)->id;
            Add(world,Arm(branch,0,4,reverse));
            const auto shape = side == 2 ? Shapes[0] : Shapes[1];
            Expect(world,shape,turn);
            Check(world.GetRoadAt(Center,Center)->id == id, "Corner upgrade replaced its ID");
            const auto revision = world.GetRoadRevision();
            const auto reuse = world.PlaceRoads(Arm(branch,0,4,reverse));
            Check(!reuse.changed && !reuse.evaluation.blocked && world.GetRoadRevision() == revision,
                  "Existing junction was charged again");
            Add(world,Arm(Direction(side == 0 ? 2 : 0,turn),0,4,reverse));
            Expect(world,Shapes[2],turn);
            Check(world.GetRoadAt(Center,Center)->id == id, "Double upgrade replaced its ID");
            // No five-way asset: reject the extra arm without breaking existing roads.
            const auto rejected = world.PlaceRoads(Arm(Direction(1,turn),0,4,reverse));
            Check(rejected.evaluation.blocked > 0, "Unsupported fifth port accepted");
            Expect(world,Shapes[2],turn); CheckNetwork(world);
        }
        for (unsigned turn=0; turn<4; ++turn) for (bool reverse : {false,true})
        {
            auto world = FlatWorld();
            Add(world,Arm(Direction(0,turn),-4,4,reverse));
            Add(world,Arm(Direction(5,turn),0,4,reverse));
            Add(world,Arm(Direction(6,turn),0,4,reverse));
            Expect(world,Shapes[2],turn);
            // A cardinal branch can also exist first. Leave the junction's
            // center empty, then fill it when drawing the second diagonal arm.
            for (int side : {0,2})
            {
                auto branchFirst = FlatWorld();
                Add(branchFirst,Arm(Direction(side,turn),1,4,reverse));
                Add(branchFirst,Arm(Direction(5,turn),1,4,reverse));
                Add(branchFirst,Arm(Direction(6,turn),0,4,reverse));
                Expect(branchFirst,side == 2 ? Shapes[0] : Shapes[1],turn);
            }
        }
        std::cout << "PASS: three corner junctions, four rotations, build/drag orders, both T-to-double upgrades, reuse and unsupported fifth arms\n";
    }
    void CheckBlocked()
    {
        for (int side : {0,2}) for (int obstacle=0; obstacle<4; ++obstacle)
        {
            auto world = FlatWorld(); Add(world,Arm(5)); Add(world,Arm(6));
            const auto before = *world.GetRoadAt(Center,Center);
            const int x = Center + DX[side];
            if (obstacle == 0) world.GetTile(x,Center).buildingId = 42;
            if (obstacle == 1) world.GetTile(x,Center).infrastructureId = 12345;
            if (obstacle >= 2)
            {
                auto& t = const_cast<TerrainHeightmap&>(world.GetTerrain());
                t.heights[Center*t.dimensions.CornerStride()+x] = obstacle == 2 ? -1.0f : 2.0f;
            }
            const auto result = world.PlaceRoads(Arm(side));
            Check(result.evaluation.blocked > 0, "Blocked junction accepted");
            const auto* center = world.GetRoadAt(Center,Center);
            Check(center->id == before.id && center->connections == before.connections &&
                  center->diagonalAxis == before.diagonalAxis && !center->mixedConnections,
                  "Rejected branch damaged the existing corner");
            CheckNetwork(world);
        }
        std::cout << "PASS: obstructed, wet and uneven junctions preserve the existing corner\n";
    }
    void CheckSeams(const std::filesystem::path& assets)
    {
        for (auto shape : Shapes)
        {
            const auto& asset = RoadAssets[static_cast<std::size_t>(shape)];
            const auto mesh = LoadRoadMesh(assets/asset.model,asset);
            Check(mesh.hasTexcoords, "Authored UVs were lost");
            for (const auto& v : mesh.vertices)
                Check(std::isfinite(v.u) && std::isfinite(v.v), "Invalid fitted UVs");
            for (int d=0; d<8; ++d) if (asset.connections & (1 << d)) for (float side : {-1.f,1.f})
            {
                const float width = .25f / (d >= 4 ? RoadDiagonalLength : 1.f);
                const float x = DX[d]*.5f-DZ[d]*side*width, z = DZ[d]*.5f+DX[d]*side*width;
                Check(std::any_of(mesh.vertices.begin(),mesh.vertices.end(),[&](const auto& v) { return Near(v.x,x) && Near(v.z,z); }),
                      "Junction mouth misses the eight-meter road seam");
            }
        }
        std::cout << "PASS: fitted GLBs retain UVs and match all cardinal/diagonal road mouths\n";
    }
}
int main(int argc,char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: CityDiagonalCornerIntersectionTests assets-directory");
        CheckSeams(argv[1]); CheckPlacement(); CheckBlocked(); return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
