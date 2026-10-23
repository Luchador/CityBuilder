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
            if (!HasRoadModel(a))
            {
                const auto* owner = world.GetRoadModuleAt(a.tileX, a.tileZ);
                Check(owner && HasRoadModel(*owner) && RoadModulePortsAt(owner->mixedConnections,
                    a.tileX-owner->tileX, a.tileZ-owner->tileZ) == a.modulePorts,
                    "Covered road has no matching model owner");
            }
            for (int dz=-RoadFootprintRadius; dz<=RoadFootprintRadius; ++dz)
                for (int dx=-RoadFootprintRadius; dx<=RoadFootprintRadius; ++dx)
                    if (RoadReservesTile(a,a.tileX+dx,a.tileZ+dz))
                        Check(world.GetTile(a.tileX+dx,a.tileZ+dz).infrastructureId != NoInfrastructure &&
                              world.GetTile(a.tileX+dx,a.tileZ+dz).zoneType == ZoneType::None,
                              "Road footprint lost its reservation or kept zoning");
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
                    // Three-way corner junctions have no through route;
                    // their build sequences are covered by the corner tests.
                    if (through < 0) continue;
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
        std::cout << "PASS: through-route junctions, all quarter rotations, both build/drag orders, stable IDs, reuse and collision-free topology\n";
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
        // The short branch is now integrated into the three-tile junction,
        // so it no longer draws an overlapping standalone flared cap.
        auto shortBranch = FlatWorld();
        shortBranch.PlaceRoads(Arm(4, -4, 4));
        Check(!shortBranch.PlaceRoads(Arm(2, 0, 1)).evaluation.blocked,
              "Short integrated branch was blocked");
        Check(!HasRoadModel(*shortBranch.GetRoadAt(Center+1,Center)), "Integrated branch drew an extra cap");
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
            if (asset.moduleCells.size() >= 2)
                for (int d=0;d<8;++d) if (asset.connections & (1 << d))
                {
                    const float length=std::hypot(static_cast<float>(DX[d]),static_cast<float>(DZ[d]));
                    const float reach=(RoadModulePortsAt(asset.connections,DX[d],DZ[d]) ? 1.5f : .5f)*length;
                    bool left=false,right=false;
                    for (const auto& v : mesh.vertices)
                    {
                        const float along=(v.x*DX[d]+v.z*DZ[d])/length;
                        const float across=(-v.x*DZ[d]+v.z*DX[d])/length;
                        if (std::abs(along-reach)>.00001f) continue;
                        left |= std::abs(across+.25f)<.00001f;
                        right |= std::abs(across-.25f)<.00001f;
                    }
                    Check(left && right,"Slip approach is not eight meters wide at its far tile seam");
                    // Endpoint checks alone miss a gap in the middle of an
                    // extended mouth. Its boundary must cover the full width.
                    std::vector<std::pair<float,float>> intervals;
                    for (std::size_t i=0;i<mesh.indices.size();i+=3)
                        for (std::size_t edge=0;edge<3;++edge)
                        {
                            const auto& a=mesh.vertices[mesh.indices[i+edge]];
                            const auto& b=mesh.vertices[mesh.indices[i+(edge+1)%3]];
                            const float ap=(a.x*DX[d]+a.z*DZ[d])/length, bp=(b.x*DX[d]+b.z*DZ[d])/length;
                            if (std::abs(ap-reach)>.00001f || std::abs(bp-reach)>.00001f) continue;
                            const float aq=(-a.x*DZ[d]+a.z*DX[d])/length, bq=(-b.x*DZ[d]+b.z*DX[d])/length;
                            const float low=std::max(-.25f,std::min(aq,bq)), high=std::min(.25f,std::max(aq,bq));
                            if (high>low) intervals.emplace_back(low,high);
                        }
                    std::sort(intervals.begin(),intervals.end());
                    float end=-.25f;
                    for (const auto& [low,high] : intervals)
                    {
                        Check(low<=end+.00001f,"Gap within a road mouth");
                        end=std::max(end,high);
                    }
                    Check(end>=.24999f,"Incomplete road mouth");
                }
            for (const auto& v : mesh.vertices)
                Check((std::abs(v.x) + std::abs(v.z) <= 1.00001f ||
                      std::any_of(asset.moduleCells.begin(),asset.moduleCells.end(),[&](auto cell)
                          { return (cell.ports & RoadAllDiagonals) ? std::abs(v.x-cell.x) + std::abs(v.z-cell.z) <= 1.00001f :
                              std::abs(v.x-cell.x) <= .50001f && std::abs(v.z-cell.z) <= .50001f; })) &&
                      std::abs(v.y) < .00001f && v.ny > .999f,
                      "Mixed mesh escaped footprint or lost upward normals");
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                const auto& a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]], c = mesh.vertices[mesh.indices[i + 2]];
                Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) > 0, "Mixed triangle faces down");
            }
        }
        std::cout << "PASS: actual GLB loading, collision profiles, footprint bounds and backface winding\n";
    }

    void CheckSlipModules()
    {
        for (const auto& asset : RoadAssets) if (asset.moduleCells.size() >= 2)
            for (unsigned turn=0; turn<4; ++turn)
            {
                auto world=FlatWorld();
                const auto mask=RotateRoadConnections(asset.connections,turn);
                for (int d=0; d<8; ++d) if (mask & (1 << d))
                    Check(!world.PlaceRoads(Arm(d,1)).evaluation.blocked,"Slip approach fixture failed");
                Check(world.PlaceRoad(Center,Center),"Slip gap fill failed");
                const auto* owner=world.GetRoadAt(Center,Center);
                const auto id=owner->id;
                std::size_t covered=0;
                ForEachRoadModuleCell(mask,[&](auto cell)
                {
                    const auto* child=world.GetRoadAt(Center+cell.x,Center+cell.z);
                    Check(child && !HasRoadModel(*child) && child->modulePorts == cell.ports &&
                        world.GetRoadModuleAt(child->tileX,child->tileZ)->id == id,"Wrong slip tile ownership");
                    ++covered;
                    const auto childId=child->id;
                    const auto revision=world.GetRoadRevision();
                    Check(!world.PlaceRoad(child->tileX,child->tileZ) && revision==world.GetRoadRevision(),
                        "Clicking a covered road charged again");
                    const auto unsupported=static_cast<std::uint8_t>(15 & ~cell.ports);
                    Check(!world.PlaceRoad(child->tileX,child->tileZ,unsupported),"Covered approach became a junction");
                    Check(world.GetRoadAt(Center+cell.x,Center+cell.z)->id==childId,"Covered tile lost its ID");
                });
                Check(covered==asset.moduleCells.size() &&
                    std::count_if(world.GetRoads().begin(),world.GetRoads().end(),HasRoadModel)==
                        static_cast<std::ptrdiff_t>(world.GetRoads().size()-covered),"Duplicate slip model instances");
                CheckNetwork(world);
            }
        // An obstacle on the far shoulder of an owned diagonal cell must be
        // checked even though it is two tiles from the junction anchor.
        for (unsigned turn=0; turn<4; ++turn) for (int kind=0; kind<4; ++kind)
        {
            auto world=FlatWorld();
            const int through=std::countr_zero(static_cast<unsigned>(RotateRoadConnections(RoadWest,turn)));
            const int branch=std::countr_zero(static_cast<unsigned>(RotateRoadConnections(RoadNorthWest,turn)));
            Check(!world.PlaceRoads(Arm(through,-4,4)).evaluation.blocked,"Blocked slip fixture failed");
            const auto before=*world.GetRoadAt(Center,Center);
            int dx=-2,dz=-1;
            for (unsigned i=0;i<turn;++i) { const int x=dx;dx=-dz;dz=x; }
            const int x=Center+dx,z=Center+dz;
            if (kind==0) world.GetTile(x,z).buildingId=123;
            if (kind==1) world.GetTile(x,z).infrastructureId=123456;
            if (kind>=2)
            {
                auto& terrain=const_cast<TerrainHeightmap&>(world.GetTerrain());
                terrain.heights[z*terrain.dimensions.CornerStride()+x]=kind==2 ? -1.f : 2.f;
            }
            Check(world.PlaceRoads(Arm(branch)).evaluation.blocked>0,"Blocked far shoulder accepted");
            const auto* center=world.GetRoadAt(Center,Center);
            Check(center->id==before.id && center->connections==before.connections && !center->mixedConnections,
                "Rejected slip upgrade damaged the original road");
            CheckNetwork(world);
        }
        std::cout << "PASS: multi-tile ownership, single model instances, reuse, protected approaches and far-shoulder obstacles\n";
    }

    void CheckGridCurveJunctions()
    {
        for (const auto shape : {RoadShape::GridCurveInnerThreeWay, RoadShape::GridCurveOuterThreeWay})
            for (unsigned turn=0;turn<4;++turn)
                for (bool branchFirst : {false,true}) for (bool reverse : {false,true})
                {
                    auto world=FlatWorld();
                    const auto direction=[&](int d)
                    { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d),turn))); };
                    const int w=direction(0),n=direction(1);
                    const int branch=direction(shape==RoadShape::GridCurveInnerThreeWay ? 4 : 6);
                    RoadPath curve{Center+4*DX[w],Center+4*DZ[w],Center+4*DX[n],Center+4*DZ[n],DX[w]!=0};
                    const auto add=[&](RoadPath path)
                    {
                        if (reverse)
                        {
                            std::swap(path.startX,path.endX);std::swap(path.startZ,path.endZ);
                            // Reversing an L also swaps its first axis, keeping the same bend.
                            if (!path.diagonal) path.xFirst=!path.xFirst;
                        }
                        const auto plan=world.PlanRoads(path);
                        Check(plan.evaluation.valid && !plan.evaluation.blocked,"Grid curve approach was rejected");
                        Check(world.PlaceRoads(path).changed==plan.evaluation.changes,"Grid curve plan/commit mismatch");
                        CheckNetwork(world);
                    };
                    add(branchFirst ? Arm(branch) : curve);
                    const auto id=world.GetRoadAt(Center,Center)->id;
                    add(branchFirst ? curve : Arm(branch));
                    const auto mask=RotateRoadConnections(RoadAssets[static_cast<std::size_t>(shape)].connections,turn);
                    const auto* center=world.GetRoadAt(Center,Center);
                    Check(center->id==id && center->connections==mask && center->mixedConnections==mask &&
                          GetRoadVisual(*center).shape==shape,"Wrong grid curve junction or changed identity");
                    const auto revision=world.GetRoadRevision();
                    add(Arm(branch));add(curve);
                    Check(world.GetRoadRevision()==revision,"Reused grid curve junction charged/changed");
                    std::vector<Road> children;
                    ForEachRoadModuleCell(mask,[&](auto cell)
                    { children.push_back(*world.GetRoadAt(Center+cell.x,Center+cell.z)); });
                    add(Arm((branch-4+2)%4+4));
                    center=world.GetRoadAt(Center,Center);
                    Check(center->id==id && GetRoadVisual(*center).shape==RoadShape::MixedFourWayArrow,
                          "Grid curve junction did not upgrade to the arrow");
                    for (const auto& child : children)
                        Check(world.GetRoadAt(child.tileX,child.tileZ)->id==child.id &&
                              world.GetRoadModuleAt(child.tileX,child.tileZ)->id==id,
                              "Arrow upgrade lost existing covered-tile identity/ownership");
                }
        for (int branch : {4,6}) for (unsigned turn=0;turn<4;++turn) for (int kind=0;kind<4;++kind)
        {
            auto world=FlatWorld();
            const auto direction=[&](int d)
            { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d),turn))); };
            const int w=direction(0),n=direction(1),d=direction(branch);
            Check(!world.PlaceRoads(Arm(w)).evaluation.blocked && !world.PlaceRoads(Arm(n)).evaluation.blocked,
                  "Blocked grid curve fixture failed");
            const auto before=*world.GetRoadAt(Center,Center);
            const int x=Center+DX[d],z=Center+DZ[d];
            if (kind==0) world.GetTile(x,z).buildingId=123;
            if (kind==1) world.GetTile(x,z).infrastructureId=123456;
            if (kind>=2)
            {
                // Alter the far corner so the original grid bend stays level.
                auto& terrain=const_cast<TerrainHeightmap&>(world.GetTerrain());
                terrain.heights[(z+(DZ[d]>0))*terrain.dimensions.CornerStride()+x+(DX[d]>0)]=kind==2 ? -1.f : 2.f;
            }
            Check(world.PlaceRoads(Arm(d)).evaluation.blocked>0,"Obstructed grid curve junction accepted");
            const auto* center=world.GetRoadAt(Center,Center);
            Check(center->id==before.id && center->connections==before.connections && !center->mixedConnections,
                  "Rejected grid curve upgrade damaged the original bend");
            CheckNetwork(world);
        }
        std::cout << "PASS: inner/outer grid curve junctions, all rotations and build/drag orders, reuse, arrow upgrades and blocked-upgrade fallback\n";
    }

    void CheckArrowUpgrades()
    {
        const auto& asset=RoadAssets[static_cast<std::size_t>(RoadShape::MixedFourWayArrow)];
        for (unsigned turn=0;turn<4;++turn) for (int first : {0,1}) for (bool reverse : {false,true})
        {
            auto world=FlatWorld();
            const auto direction=[&](int d)
            { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d),turn))); };
            const auto add=[&](RoadPath path)
            {
                if (reverse) { std::swap(path.startX,path.endX);std::swap(path.startZ,path.endZ); }
                const auto plan=world.PlanRoads(path);
                Check(!plan.evaluation.blocked,"Arrow approach was rejected");
                Check(world.PlaceRoads(path).changed==plan.evaluation.changes,"Arrow plan/commit mismatch");
                CheckNetwork(world);
            };
            add(Arm(direction(4),-4,4));
            const auto id=world.GetRoadAt(Center,Center)->id;
            add(Arm(direction(first)));
            Check(std::popcount(world.GetRoadAt(Center,Center)->mixedConnections)==3,"Missing intermediate T");
            add(Arm(direction(1-first)));
            const auto* arrow=world.GetRoadAt(Center,Center);
            const auto mask=RotateRoadConnections(asset.connections,turn);
            Check(arrow->id==id && arrow->connections==mask && arrow->mixedConnections==mask &&
                  GetRoadVisual(*arrow).shape==RoadShape::MixedFourWayArrow,"Wrong T-to-arrow upgrade");
            const auto before=*arrow;
            Check(world.PlaceRoads(Arm(direction(2))).evaluation.blocked>0,"Unsupported fifth arrow arm accepted");
            Check(world.GetRoadAt(Center,Center)->connections==before.connections &&
                  world.GetRoadAt(Center,Center)->id==id,"Fifth arm damaged the arrow");
            CheckNetwork(world);
        }
        for (unsigned turn=0;turn<4;++turn) for (int kind=0;kind<4;++kind)
        {
            auto world=FlatWorld();
            const auto direction=[&](int d)
            { return std::countr_zero(static_cast<unsigned>(RotateRoadConnections(static_cast<std::uint8_t>(1 << d),turn))); };
            Check(!world.PlaceRoads(Arm(direction(4),-4,4)).evaluation.blocked,"Arrow obstacle fixture failed");
            Check(!world.PlaceRoads(Arm(direction(0))).evaluation.blocked,"Arrow obstacle T failed");
            const auto before=*world.GetRoadAt(Center,Center);
            const int north=direction(1),x=Center+DX[north],z=Center+DZ[north];
            if (kind==0) world.GetTile(x,z).buildingId=123;
            if (kind==1) world.GetTile(x,z).infrastructureId=123456;
            if (kind>=2)
            {
                auto& terrain=const_cast<TerrainHeightmap&>(world.GetTerrain());
                terrain.heights[z*terrain.dimensions.CornerStride()+x]=kind==2 ? -1.f : 2.f;
            }
            Check(world.PlaceRoads(Arm(north)).evaluation.blocked>0,"Blocked arrow upgrade accepted");
            const auto* center=world.GetRoadAt(Center,Center);
            Check(center->id==before.id && center->mixedConnections==before.mixedConnections &&
                  center->connections==before.connections,"Blocked arrow upgrade changed the existing T");
            CheckNetwork(world);
        }
        std::cout << "PASS: both T-to-arrow upgrades, all rotations/drag directions, fifth-arm rejection and blocked-upgrade fallback\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: CityMixedRoadTests assets-directory");
        CheckGeometry(argv[1]); CheckAllJunctions(); CheckBendUpgrades(); CheckMissingCenters(); CheckObstacles(); CheckSlipModules(); CheckArrowUpgrades(); CheckGridCurveJunctions();
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
