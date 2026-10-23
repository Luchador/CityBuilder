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
            if (!HasRoadModel(a))
            {
                const auto* owner = world.GetRoadModuleAt(a.tileX,a.tileZ);
                Check(owner && HasRoadModel(*owner) &&
                      RoadModulePortsAt(owner->mixedConnections,a.tileX-owner->tileX,a.tileZ-owner->tileZ) == a.modulePorts &&
                      (a.connections & ~a.modulePorts) == 0, "Invalid module membership");
            }
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
    void CheckModules()
    {
        for (unsigned turn=0; turn<4; ++turn)
        {
            const int inside=Direction(2,turn), outside=Direction(0,turn);
            const int x=Center+DX[inside], z=Center+DZ[inside];
            auto world=FlatWorld();
            Add(world,Arm(Direction(5,turn))); Add(world,Arm(Direction(6,turn)));
            const auto ownerId=world.GetRoadAt(Center,Center)->id;
            // One new inside tile is enough. It belongs to the junction's
            // straight, rather than drawing the standalone flared end model.
            const auto result=world.PlaceRoads(Arm(inside,0,1));
            Check(!result.evaluation.blocked && result.changed == 2 && result.evaluation.upgrades == 1,
                  "Short integrated branch failed");
            Expect(world,Shapes[0],turn); CheckNetwork(world);
            const auto childId=world.GetRoadAt(x,z)->id;
            Check(!HasRoadModel(*world.GetRoadAt(x,z)) &&
                  world.GetRoadModuleAt(x,z) == world.GetRoadAt(Center,Center) &&
                  world.GetRoadModuleAt(Center,Center)->id == ownerId &&
                  world.GetRoadAt(x,z)->modulePorts == RotateRoadConnections(RoadWest|RoadEast,turn),
                  "Inside cell is not owned by the corner model");
            Check(std::count_if(world.GetRoads().begin(),world.GetRoads().end(),HasRoadModel) ==
                  static_cast<std::ptrdiff_t>(world.GetRoads().size()-1), "Module would be drawn twice");
            Check(world.GetTile(x,z).infrastructureId == childId && world.GetTile(x,z).zoneType == ZoneType::None,
                  "Integrated cell lost occupancy or retained zoning");
            const auto revision=world.GetRoadRevision();
            Check(!world.PlaceRoad(x,z) && revision == world.GetRoadRevision(), "Clicking the covered tile charged again");
            Add(world,Arm(inside,1,4)); // Continue the road at the far edge.
            Add(world,Arm(outside));
            Expect(world,Shapes[2],turn);
            Check(world.GetRoadAt(x,z)->id == childId && world.GetRoadModuleAt(x,z)->id == ownerId,
                  "Acute-to-double upgrade lost tile/module identity");
            const auto memberBefore=*world.GetRoadAt(x,z);
            const auto across=RotateRoadConnections(RoadNorth|RoadSouth,turn);
            Check(!world.PlaceRoad(x,z,across), "Covered straight converted independently into a junction");
            const int side=Direction(1,turn);
            const auto blocked=world.PlaceRoads({x,z,x+DX[side],z+DZ[side]});
            Check(blocked.evaluation.blocked > 0 && world.GetRoadAt(x,z)->connections == memberBefore.connections &&
                  world.GetRoadAt(x,z)->modulePorts == memberBefore.modulePorts,
                  "Sideways drag damaged a covered tile");
            Expect(world,Shapes[2],turn); CheckNetwork(world);
        }
        // The broad phase must include the entire inside cell, even outside
        // the original diamond. This is an independent crossing, not a member.
        Road owner; owner.mixedConnections=RoadNorthEast|RoadSouthEast|RoadEast;
        Road crossing; crossing.tileX=1; crossing.connections=RoadNorth|RoadSouth;
        Check(RoadMeshesOverlap(owner,crossing), "Collision omitted the second tile of the model");
        Road extension; extension.tileX=2; extension.connections=RoadWest|RoadEast;
        Check(!RoadMeshesOverlap(owner,extension), "Far edge seam overlaps the next straight");
        std::cout << "PASS: two-cell ownership, short branches, single model instances, extension, stable IDs, reuse and protected inside tiles\n";
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
                const float reach = RoadModulePortsAt(asset.connections,DX[d],DZ[d]) ? 1.5f : .5f;
                const float x = DX[d]*reach-DZ[d]*side*width, z = DZ[d]*reach+DX[d]*side*width;
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
        CheckSeams(argv[1]); CheckPlacement(); CheckBlocked(); CheckModules(); return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
