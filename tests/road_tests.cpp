#include "road_tool.hpp"
#include "static_mesh.hpp"
#include "road_mesh.hpp"
#include "road_footprint.hpp"
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
#include <set>
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
        Check(controller.BuildPreview().tiles == ToolTileRect{0, 1, 4, 5} &&
              !controller.BuildPreview().zoneType && controller.BuildPreview().placementAllowed,
              "Valid road preview missing");
        world.GetTile(2, 3).zoneType = ZoneType::ResidentialLight;
        Check(controller.Begin(At(2, 3)), "Valid road click rejected");
        const auto request = std::get<RoadRequest>(controller.End(At(2, 3)));
        Check(request.path == RoadPath{2, 3, 2, 3, true} && world.GetRoads().empty(), "Tool mutated world or chose wrong tile");
        Check(world.PlaceRoads(request.path).changed == 1, "Placement rejected");
        const auto& road = world.GetRoads().front();
        Check(road.tileX == 2 && road.tileZ == 3 && road.surface.elevation == TerrainBaseElevation &&
              road.id != NoInfrastructure && world.GetTile(2, 3).infrastructureId == road.id &&
              world.GetTile(2, 3).zoneType == ZoneType::None, "Road record/occupancy/zone replacement disagree");
        Check(!world.PlaceRoad(2, 3) && world.GetRoads().size() == 1 && !CanZoneTile(world, 2, 3),
              "Duplicate road or zoning over road allowed");
        controller.Update(At(2, 3), .016f);
        Check(controller.BuildPreview().placementAllowed && controller.Begin(At(2, 3)),
              "Existing road must allow route reuse");
        const auto blocked = std::get<RoadRequest>(controller.End(At(2, 3)));
        const auto reused = world.PlaceRoads(blocked.path);
        Check(!reused.changed && reused.evaluation.unchanged == 1 && !reused.evaluation.blocked,
              "Reusing an existing road was not a no-op");
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
        Check(controller.BuildPreview().roadPath == RoadPath{3, 3, 3, 4, false}, "Drag preview incorrect");
        Check(std::get<RoadRequest>(controller.End(At(3, 4))).path == RoadPath{3, 3, 3, 4, false},
              "Drag did not produce a route");
        controller.Begin(At(3, 3));
        controller.Cancel();
        Check(Empty(controller.End(At(3, 3))), "Canceled click created a road");
        controller.Begin(At(3, 3));
        Check(Empty(controller.End({})), "UI/off-map release used stale hover");
        controller.Begin(At(3, 3));
        world.GetTile(3, 3).buildingId = 1;
        const auto newlyBlocked = std::get<RoadRequest>(controller.End(At(3, 3)));
        Check(!world.PlaceRoads(newlyBlocked.path).changed, "Commit ignored newly occupied tile");
        Check(world.PlaceRoad(15, 15), "Last map tile rejected");
        std::cout << "PASS: click/drag/cancel, bounds, flat/dry rules, occupancy, IDs and zone replacement\n";
    }

    void CheckConnectionsAndRoutes()
    {
        const int dx[] = {-1, 0, 1, 0}, dz[] = {0, -1, 0, 1};
        // Every neighbor set, both center-first and center-last: insertion order
        // must not affect topology, and the previously placed neighbors update.
        for (int mask = 0; mask < 16; ++mask) for (bool centerFirst : {false, true})
        {
            auto world = FlatWorld();
            if (centerFirst) Check(world.PlaceRoad(7, 7), "Center placement failed");
            for (int d = 0; d < 4; ++d) if (mask & (1 << d))
                Check(centerFirst ? world.PlaceRoads({7,7,7+dx[d],7+dz[d]}).changed != 0 :
                      world.PlaceRoad(7+dx[d],7+dz[d]), "Neighbor placement failed");
            if (!centerFirst) Check(world.PlaceRoad(7, 7), "Center placement failed");
            const auto* center = world.GetRoadAt(7, 7);
            Check(center && center->connections == mask, "Incorrect neighbor mask");
            const auto visual = GetRoadVisual(center->connections);
            const auto shape = static_cast<std::size_t>(visual.shape);
            if (mask) Check(RotateRoadConnections(RoadAssets[shape].connections, visual.quarterTurns) == mask,
                            "Mesh openings do not match topology");
            const int degree = std::popcount(static_cast<unsigned>(mask));
            const RoadShape expected = degree <= 1 ? RoadShape::DeadEnd : degree == 4 ? RoadShape::FourWay :
                degree == 3 ? RoadShape::ThreeWay : (mask == 5 || mask == 10) ? RoadShape::Straight : RoadShape::Curve;
            Check(visual.shape == expected, "Wrong shape for degree/turn");
            for (int d = 0; d < 4; ++d) if (mask & (1 << d))
                Check(world.GetRoadAt(7 + dx[d], 7 + dz[d])->connections == (1 << ((d + 2) % 4)),
                      "Existing neighbor was not reconnected");
            Check(world.GetRoadRevision() == world.GetRoads().size(), "Road edit revision missing");
        }
        for (const auto end : {ToolTile{11, 9}, ToolTile{3, 9}, ToolTile{3, 5}, ToolTile{11, 5},
                               ToolTile{9, 11}, ToolTile{5, 11}, ToolTile{5, 3}, ToolTile{9, 3}})
        {
            auto world = FlatWorld();
            ToolController controller(world);
            controller.SetTool(std::make_unique<RoadTool>());
            Check(controller.Begin(At(7, 7)), "Route begin failed");
            Check(Empty(controller.Update(At(end.x, end.z), .016f)) && world.GetRoads().empty(),
                  "Drag committed before release");
            const auto preview = controller.BuildPreview();
            const auto request = std::get<RoadRequest>(controller.End(At(end.x, end.z)));
            Check(preview.roadPath == request.path, "Released path differs from preview");
            const auto result = world.PlaceRoads(request.path);
            Check(result.changed == 5 && !result.evaluation.blocked, "Route length/gaps/bend duplicate wrong");
            const auto again = world.PlaceRoads(request.path);
            Check(!again.changed && again.evaluation.unchanged == 5 && !again.evaluation.blocked,
                  "Repeated route was not a no-op");
            std::set<std::pair<int, int>> selected;
            request.path.ForEachTile([&](int x, int z) { selected.emplace(x, z); });
            const int sx=end.x>7 ? 1 : -1,sz=end.z>7 ? 1 : -1;
            const int bendX=std::abs(end.x-7)>std::abs(end.z-7) ? 7+2*sx : 7;
            const int bendZ=std::abs(end.z-7)>std::abs(end.x-7) ? 7+2*sz : 7;
            Check(IsRoadTransition(world.GetRoadAt(bendX,bendZ)->mixedConnections),
                  "Mixed drag bend did not become a grid/diagonal transition");
            // The expected shortest route: two grid steps and two diagonal
            // steps. No interior tiles from the old orthogonal L are included.
            const std::set<std::pair<int,int>> expected{{7,7},{(7+bendX)/2,(7+bendZ)/2},
                {bendX,bendZ},{bendX+sx,bendZ+sz},{end.x,end.z}};
            for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x)
            {
                Check(selected.contains({x,z})==expected.contains({x,z}),"Preview route membership mismatch");
            }
        }

        auto world = FlatWorld();
        world.GetTile(4, 6).buildingId = 123;
        world.GetTile(5, 6).infrastructureId = 456;
        world.GetTile(2, 6).zoneType = ZoneType::CommercialDense;
        const RoadPath line{1, 6, 8, 6};
        const auto partial = world.PlaceRoads(line);
        Check(partial.changed == 3 && partial.evaluation.blocked == 5 &&
              world.GetTile(2, 6).zoneType == ZoneType::None, "Partial route failed");
        Check(world.GetRoadAt(3, 6)->connections == RoadWest && !world.GetRoadAt(6, 6),
              "Road continued beyond its first blocked gap");
        const auto rejected = world.PlaceRoads({4, 6, 5, 6});
        Check(!rejected.changed && rejected.evaluation.blocked == 2, "Fully blocked route accepted");
        Check(!world.PlaceRoads({-1, 0, 15, 0}).evaluation.valid &&
              !world.PlaceRoads({0, 0, std::numeric_limits<int>::max(), 0}).evaluation.valid,
              "Off-map/unbounded route traversed");
        Check(!world.GetRoadAt(-1, 0) && !world.GetRoadAt(16, 0), "Off-map neighbor lookup failed");
        auto edge = FlatWorld();
        Check(edge.PlaceRoads({0, 0, 15, 0}).changed == 16 &&
              edge.PlaceRoads({15, 0, 15, 15}).changed == 15, "Edge route incorrect");
        Check(edge.GetRoadAt(15, 0)->connections == (RoadWest | RoadSouth), "Map-edge curve incorrect");
        std::cout << "PASS: all 16 connection masks, insertion order, all drag directions, exact L previews, partial/reused/blocked routes and map edges\n";
    }

    void CheckMixedDrags()
    {
        constexpr int portX[]{-1,0,1,0,-1,1,1,-1},portZ[]{0,-1,0,1,-1,-1,1,1};
        for (int dx=-5;dx<=5;++dx) for (int dz=-5;dz<=5;++dz)
            for (bool diagonalFirst : {false,true})
        {
            auto path=RoadPath::FromDrag(7,7,7+dx,7+dz);
            if (diagonalFirst && !path.mixed) continue;
            path.diagonalFirst=diagonalFirst;
            auto world=FlatWorld();
            std::vector<RoadPath::Point> ordered;
            path.ForEachTile([&](int x,int z) { ordered.push_back({x,z}); });
            const auto steps=std::max(std::abs(dx),std::abs(dz));
            Check(path.HasValidShape() && ordered.size()==static_cast<std::size_t>(steps+1) &&
                  ordered.back().x==7+dx && ordered.back().z==7+dz,"Mixed path missed its target or used excess tiles");
            int diagonalSteps=0;
            for (std::size_t i=0;i<ordered.size();++i)
            {
                const auto p=ordered[i];
                Check(path.Contains(p.x,p.z) && path.IndexOf(p.x,p.z)==i,"Mixed path index/traversal mismatch");
                std::uint8_t expected=0;
                for (int d=0;d<8;++d)
                    if ((i && ordered[i-1].x-p.x==portX[d] && ordered[i-1].z-p.z==portZ[d]) ||
                        (i+1<ordered.size() && ordered[i+1].x-p.x==portX[d] && ordered[i+1].z-p.z==portZ[d]))
                        expected |= static_cast<std::uint8_t>(1 << d);
                Check(path.ConnectionsAt(p.x,p.z)==expected,"Mixed path added a shortcut or lost a route edge");
                if (i && p.x!=ordered[i-1].x && p.z!=ordered[i-1].z) ++diagonalSteps;
            }
            Check(diagonalSteps==std::min(std::abs(dx),std::abs(dz)),"Mixed path did not maximize efficient diagonal travel");
            const auto plan=world.PlanRoads(path);
            if (plan.evaluation.blocked)
                throw std::runtime_error("Empty-map mixed route rejected: dx="+std::to_string(dx)+" dz="+std::to_string(dz)+
                    " diagonalFirst="+std::to_string(diagonalFirst));
            Check(world.PlaceRoads(path).changed==ordered.size(),"Mixed plan/commit mismatch");
            for (const auto& a : world.GetRoads())
            {
                Check(a.connections==path.ConnectionsAt(a.tileX,a.tileZ),"Mixed route grew an incidental connection");
                for (const auto& b : world.GetRoads()) if (a.id<b.id)
                    Check(!RoadMeshesOverlap(a,b),"Mixed route pavement overlaps");
            }
            if (path.mixed)
            {
                const auto bend=path.Bend();
                Check(IsRoadTransition(world.GetRoadAt(bend.x,bend.z)->mixedConnections),"Mixed bend has no transition mesh");
            }
            Check(!world.PlaceRoads(path).changed,"Repeating a mixed drag changed the network");
        }
        for (bool blockGridFirst : {false,true})
        {
            auto world=FlatWorld();
            world.GetTile(4,blockGridFirst ? 2 : 4).buildingId=123;
            const auto route=world.PlanRoadDrag(2,2,12,6);
            Check(route.path.diagonalFirst==blockGridFirst && !route.plan.evaluation.blocked && route.plan.evaluation.changes==11,
                  "Routing failed to choose the clear shortest alternative");
            ToolController controller(world);
            controller.SetTool(std::make_unique<RoadTool>());
            controller.Begin(At(2,2));controller.Update(At(12,6),.016f);
            const auto preview=controller.BuildPreview();
            const auto request=std::get<RoadRequest>(controller.End(At(12,6)));
            Check(preview.roadPath==route.path && request.path==route.path &&
                  world.PlaceRoads(request.path).changed==route.plan.evaluation.changes,
                  "Chosen route differed between hover, release and commit");
        }
        auto reuse=FlatWorld();
        reuse.PlaceRoads({8,6,9,6});
        auto gridFirst=RoadPath::FromDrag(2,2,12,6),diagFirst=gridFirst;
        diagFirst.diagonalFirst=true;
        Check(!reuse.PlanRoads(gridFirst).evaluation.blocked && !reuse.PlanRoads(diagFirst).evaluation.blocked,
              "Reuse route fixture does not offer two clear alternatives");
        const auto reused=reuse.PlanRoadDrag(2,2,12,6);
        Check(reused.path.diagonalFirst && reused.plan.evaluation.changes==9,"Equal-length route did not favor existing roads");
        auto blocked=FlatWorld();
        blocked.GetTile(4,2).buildingId=1;blocked.GetTile(5,5).buildingId=2;
        const auto partial=blocked.PlanRoadDrag(2,2,12,6);
        Check(partial.path.diagonalFirst && partial.plan.evaluation.changes==3 && partial.plan.evaluation.blocked==8 &&
              blocked.PlaceRoads(partial.path).changed==3,"Blocked alternatives did not retain the best section from the start");
        Check(!blocked.PlanRoadDrag(-1,2,12,6).plan.evaluation.valid &&
              !blocked.PlanRoadDrag(2,2,std::numeric_limits<int>::max(),6).plan.evaluation.valid,
              "Out-of-map automatic routing was not bounded");
        std::cout << "PASS: shortest exact-target mixed drags in every octant, both leg orders, transition meshes, clear alternatives, road reuse and partial fallback\n";
    }

    void CheckFirstRoadSection()
    {
        const RoadPath paths[]{
            {2,6,12,6}, {6,2,6,12},
            {2,2,12,12,true,true}, {2,12,12,2,true,true},
            {2,2,12,8,true}, {2,2,8,12,false}
        };
        for (const auto& original : paths) for (bool reverse : {false,true})
            for (bool startBlocked : {false,true}) for (bool infrastructure : {false,true})
        {
            auto world=FlatWorld();
            auto path=original;
            if (reverse)
            {
                std::swap(path.startX,path.endX);std::swap(path.startZ,path.endZ);
                if (!path.diagonal) path.xFirst=!path.xFirst;
            }
            std::vector<ToolTile> ordered;
            path.ForEachTile([&](int x,int z)
            { ordered.push_back({x,z});world.GetTile(x,z).zoneType=ZoneType::CommercialLight; });
            const auto block=[&](std::size_t index)
            {
                auto& tile=world.GetTile(ordered[index].x,ordered[index].z);
                if (infrastructure) tile.infrastructureId=999;
                else tile.buildingId=999;
            };
            block(3);block(7);
            if (startBlocked) block(0);
            const auto plan=world.PlanRoads(path);
            const auto expected=static_cast<std::uint32_t>(startBlocked ? 2 : 3);
            Check(plan.evaluation.changes==expected && plan.evaluation.blocked==ordered.size()-expected,
                  "Partial drag did not choose the first valid section");
            for (std::size_t i=0;i<ordered.size();++i)
                Check(RoadPlacementAccepted(plan.tiles[i].status)==(i<3 && (!startBlocked || i>0)),
                      "Plan retained a tile outside the first valid section");
            Check(plan.tiles[4].status==RoadPlacementStatus::DisconnectedSegment,
                  "Otherwise valid later section was not explicitly invalidated");
            const auto placed=world.PlaceRoads(path);
            Check(placed.changed==expected && placed.evaluation.blocked==plan.evaluation.blocked,
                  "Partial drag plan/commit mismatch");
            for (std::size_t i=0;i<ordered.size();++i)
            {
                const auto [x,z]=ordered[i];
                const bool kept=i<3 && (!startBlocked || i>0);
                Check((world.GetRoadAt(x,z)!=nullptr)==kept,"An excluded section created a road");
                Check(world.GetTile(x,z).zoneType==(kept ? ZoneType::None : ZoneType::CommercialLight),
                      "Excluded section cleared an existing zone");
            }
            const auto revision=world.GetRoadRevision();
            Check(!world.PlaceRoads(path).changed && world.GetRoadRevision()==revision,
                  "Repeating a blocked drag advanced into a later section");
        }

        for (float height : {-TerrainHeightStep,TerrainBaseElevation+TerrainHeightStep})
        {
            auto world=FlatWorld();
            auto& terrain=const_cast<TerrainHeightmap&>(world.GetTerrain());
            terrain.heights[6*terrain.dimensions.CornerStride()+6]=height;
            Check(world.PlaceRoads({2,6,12,6}).changed==3 && !world.GetRoadAt(7,6),
                  "Water/uneven terrain did not cut off a later valid section");
        }
        auto bend=FlatWorld();
        bend.GetTile(6,7).buildingId=123;
        Check(bend.PlaceRoads({2,2,6,12,true}).changed==9 &&
              GetRoadVisual(*bend.GetRoadAt(6,2)).shape==RoadShape::Curve && !bend.GetRoadAt(6,8),
              "First section failed to follow an L around its bend");
        auto blocked=FlatWorld();
        for (int x=2;x<=12;++x) blocked.GetTile(x,6).buildingId=123;
        Check(!blocked.PlaceRoads({2,6,12,6}).changed && blocked.GetRoads().empty(),
              "Entirely blocked drag created a road");

        auto toolWorld=FlatWorld();
        toolWorld.GetTile(6,6).buildingId=123;
        ToolController controller(toolWorld);
        controller.SetTool(std::make_unique<RoadTool>());
        Check(controller.Begin(At(2,6)),"Partial tool drag failed to begin");
        controller.Update(At(12,6),.016f);
        const auto request=std::get<RoadRequest>(controller.End(At(12,6)));
        Check(!controller.BuildPreview().roadPath && toolWorld.PlaceRoads(request.path).changed==4,
              "Releasing a partial drag retained its preview or built extra sections");

        // Reused roads are traversable parts of the first section. Later
        // existing roads must stay intact, without upgrades or new neighbors.
        auto reused=FlatWorld();
        reused.PlaceRoads({2,6,4,6});reused.PlaceRoads({10,6,12,6});
        reused.GetTile(6,6).buildingId=123;
        const auto far=*reused.GetRoadAt(10,6);
        const auto result=reused.PlaceRoads({2,6,12,6});
        Check(result.changed==1 && result.evaluation.unchanged==3 && reused.GetRoadAt(5,6) &&
              !reused.GetRoadAt(7,6) && !reused.GetRoadAt(8,6) && !reused.GetRoadAt(9,6) &&
              reused.GetRoadAt(10,6)->id==far.id && reused.GetRoadAt(10,6)->connections==far.connections,
              "Trimming skipped reused roads or modified an excluded existing road");

        // Do not reopen a parallel-road edge in a discarded upgrade section.
        auto parallel=FlatWorld();
        parallel.PlaceRoads({2,10,12,10});parallel.PlaceRoads({2,11,12,11});
        parallel.GetTile(7,6).buildingId=456;
        const auto lower=*parallel.GetRoadAt(7,11);
        Check(parallel.PlaceRoads({7,2,7,11}).changed==4 &&
              parallel.GetRoadAt(7,10)->connections==(RoadWest|RoadEast) &&
              parallel.GetRoadAt(7,11)->connections==lower.connections &&
              parallel.GetRoadAt(7,11)->suppressedConnections==lower.suppressedConnections,
              "Discarded connector reopened an existing parallel edge");

        // A later diagonal junction upgrade is likewise excluded.
        auto diagonal=FlatWorld();
        diagonal.PlaceRoads({3,3,11,11,true,true});
        diagonal.GetTile(4,10).buildingId=789;
        const auto center=*diagonal.GetRoadAt(7,7);
        Check(diagonal.PlaceRoads({2,12,12,2,true,true}).changed==2 &&
              diagonal.GetRoadAt(7,7)->id==center.id && diagonal.GetRoadAt(7,7)->connections==center.connections &&
              diagonal.GetRoadAt(7,7)->diagonalAxis==center.diagonalAxis,
              "Discarded diagonal section upgraded an existing junction");
        std::cout << "PASS: nearest continuous section, leading obstacles, both drag directions, cardinal/diagonal/L paths, reused roads and excluded upgrades/zones\n";
    }

    void CheckParallelRoads()
    {
        for (bool alongX : {false,true}) for (int side : {-1,1})
            for (bool reverse : {false,true}) for (bool staggered : {false,true})
        {
            auto world=FlatWorld();
            const auto tile=[&](int along,int across) { return alongX ? ToolTile{along,across} : ToolTile{across,along}; };
            const auto line=[&](int from,int to,int across)
            {
                const auto a=tile(from,across),b=tile(to,across);
                return reverse ? RoadPath{b.x,b.z,a.x,a.z} : RoadPath{a.x,a.z,b.x,b.z};
            };
            const auto first=line(2,12,6),second=line(staggered ? 4 : 2,staggered ? 10 : 12,6+side);
            Check(!world.PlaceRoads(first).evaluation.blocked,"First parallel road failed");
            const auto plan=world.PlanRoads(second);
            Check(!plan.evaluation.blocked && world.PlaceRoads(second).changed==plan.evaluation.changes,
                  "Parallel road plan/commit mismatch");
            const auto axis=alongX ? RoadWest|RoadEast : RoadNorth|RoadSouth;
            const auto checkSeparated=[&](int connector=-1)
            {
                for (const auto& road : world.GetRoads())
                    if ((alongX ? road.tileX : road.tileZ)!=connector)
                        Check(road.connections && !(road.connections & ~axis),"Parallel road gained an incidental side edge");
            };
            checkSeparated();
            const auto revision=world.GetRoadRevision();
            Check(!world.PlaceRoads(first).changed && !world.PlaceRoads(second).changed && world.GetRoadRevision()==revision,
                  "Redrawing parallel roads changed their topology");
            const std::vector<Road> before(world.GetRoads().begin(),world.GetRoads().end());
            const auto a=tile(7,6),b=tile(7,6+side);
            const RoadPath connector=reverse ? RoadPath{b.x,b.z,a.x,a.z} : RoadPath{a.x,a.z,b.x,b.z};
            const auto connectPlan=world.PlanRoads(connector);
            Check(connectPlan.evaluation.changes && !connectPlan.evaluation.blocked &&
                  connectPlan.evaluation.changes==connectPlan.evaluation.upgrades,"Existing-only connector was not an upgrade");
            Check(world.PlaceRoads(connector).changed==connectPlan.evaluation.changes && world.GetRoads().size()==before.size(),
                  "Manual connector inserted duplicate roads or differed from preview");
            const auto toward=alongX ? (side<0 ? RoadNorth : RoadSouth) : (side<0 ? RoadWest : RoadEast);
            Check(world.GetRoadAt(a.x,a.z)->connections==(axis|toward) &&
                  world.GetRoadAt(b.x,b.z)->connections==(axis|RotateRoadConnections(toward,2)),
                  "Manual connector failed to form two reciprocal T junctions");
            checkSeparated(7);
            for (const auto& old : before)
                Check(world.GetRoadAt(old.tileX,old.tileZ)->id==old.id,"Connecting parallel roads changed an ID");
            const auto connectedRevision=world.GetRoadRevision();
            Check(!world.PlaceRoads(connector).changed && !world.PlaceRoads(first).changed && !world.PlaceRoads(second).changed &&
                  world.GetRoadRevision()==connectedRevision,"Reusing the connector or parallel routes was not a no-op");
            // A later nearby edit rebuilds the same neighborhood without
            // reopening any of the other closed side edges.
            Check(world.PlaceRoads(line(11,13,6+side)).changed>0,"Parallel extension failed");
            checkSeparated(7);
        }
        // Clicking individual tiles can also extend a parallel road. Clicking
        // an existing tile alone does not request a connection across its side.
        auto clicks=FlatWorld();
        clicks.PlaceRoads({2,6,12,6});
        for (int x=2;x<=12;++x) Check(clicks.PlaceRoad(x,7),"Clicked parallel tile failed");
        for (const auto& road : clicks.GetRoads())
            Check(!(road.connections & (RoadNorth|RoadSouth)),"Clicked parallel tiles connected sideways");
        Check(!clicks.PlaceRoad(7,7),"Single click reopened a parallel side edge");

        // A rejected endpoint must not partially reopen a persistent edge.
        const auto old=*clicks.GetRoadAt(7,7);
        clicks.GetTile(7,6).buildingId=999;
        const auto rejected=clicks.PlaceRoads({7,7,7,6});
        Check(rejected.evaluation.blocked && !rejected.changed &&
              clicks.GetRoadAt(7,7)->connections==old.connections &&
              clicks.GetRoadAt(7,7)->suppressedConnections==old.suppressedConnections,
              "Blocked connector altered the separated roads");
        clicks.GetTile(7,6).buildingId=NoBuilding;
        Check(clicks.PlaceRoads({7,7,7,6}).changed>0,"Unblocked connector failed");
        std::cout << "PASS: parallel grid roads, both axes/sides/drag directions, staggered ends, explicit connectors, stable IDs, reuse and blocked-connector fallback\n";
    }

    World RampWorld(bool alongX, bool descending)
    {
        auto world = FlatWorld();
        auto& terrain = const_cast<TerrainHeightmap&>(world.GetTerrain());
        for (unsigned z = 0; z <= 16; ++z) for (unsigned x = 0; x <= 16; ++x)
        {
            const int along = static_cast<int>(alongX ? x : z);
            terrain.heights[z * terrain.dimensions.CornerStride() + x] =
                2.0f + (descending ? -.25f : .25f) * static_cast<float>(std::clamp(along - 4, 0, 4));
        }
        return world;
    }

    void CheckSlopes()
    {
        for (bool alongX : {false, true}) for (bool descending : {false, true})
        {
            auto world = RampWorld(alongX, descending);
            const auto tile = [&](int along, int across) { return alongX ? ToolTile{along, across} : ToolTile{across, along}; };
            auto routed=RampWorld(alongX,descending);
            const auto routeStart=tile(2,8),routeEnd=tile(13,12);
            const auto route=routed.PlanRoadDrag(routeStart.x,routeStart.z,routeEnd.x,routeEnd.z);
            Check(route.path.mixed && !route.path.diagonalFirst && !route.plan.evaluation.blocked &&
                  routed.PlaceRoads(route.path).changed==12,
                  "Automatic routing did not keep the diagonal leg on the flat plateau");
            const auto a = tile(2, 8), b = tile(10, 8);
            const RoadPath path{a.x, a.z, b.x, b.z, alongX};
            Check(world.EvaluateRoads(path).changes == 9 && world.PlaceRoads(path).changed == 9,
                  "Paired slope did not accept a road route");
            const std::uint8_t axis = alongX ? RoadWest | RoadEast : RoadNorth | RoadSouth;
            for (int along = 2; along <= 10; ++along)
            {
                const auto p = tile(along, 8);
                const auto* road = world.GetRoadAt(p.x, p.z);
                const auto& t = world.GetTerrain();
                Check(Near(road->surface.HeightAt(-.5f, -.5f), t.Height(p.x, p.z)) &&
                      Near(road->surface.HeightAt(.5f, .5f), t.Height(p.x + 1, p.z + 1)),
                      "Road plane does not follow terrain corners");
                if (along > 2 && along < 10) Check(road->connections == axis, "Incline/plateau seam failed to connect");
                if (along >= 4 && along < 8)
                {
                    Check(road->surface.AllowedConnections() == axis &&
                          GetRoadVisual(*road).shape == RoadShape::Straight && !CanZoneTile(world, p.x, p.z),
                          "Slope created a junction or became zone-eligible");
                }
            }
            const auto c = tile(6, 5), d = tile(6, 11);
            const RoadPath cross{c.x, c.z, d.x, d.z, !alongX};
            const auto crossing = world.PlaceRoads(cross);
            Check(!crossing.changed && crossing.evaluation.blocked == 7 && !crossing.evaluation.unchanged,
                  "Cross-slope road or crossing an existing sloped road accepted");

            // Parallel hillside roads may coexist but must not grow sideways junctions.
            const auto pa = tile(2, 9), pb = tile(10, 9);
            Check(world.PlaceRoads({pa.x, pa.z, pb.x, pb.z, alongX}).changed == 9, "Parallel ramp rejected");
            for (int along = 4; along < 8; ++along)
            {
                const auto p = tile(along, 8);
                const auto q = tile(along, 9);
                Check(world.GetRoadAt(p.x, p.z)->connections == axis && world.GetRoadAt(q.x, q.z)->connections == axis,
                      "Parallel sloped roads connected sideways");
            }

            auto alone = RampWorld(alongX, descending);
            const auto p = tile(6, 8);
            Check(alone.PlaceRoad(p.x, p.z), "Isolated sloped end rejected");
            const auto visual = GetRoadVisual(*alone.GetRoadAt(p.x, p.z));
            const auto opening = RotateRoadConnections(RoadAssets[static_cast<std::size_t>(visual.shape)].connections, visual.quarterTurns);
            Check(visual.shape == RoadShape::DeadEnd && opening == (alongX ?
                  (descending ? RoadEast : RoadWest) : (descending ? RoadSouth : RoadNorth)),
                  "Isolated end not aligned downhill");
            const auto extension = tile(7, 8);
            Check(alone.PlaceRoad(extension.x, extension.z) && alone.GetRoadAt(p.x, p.z)->connections ==
                  (alongX ? RoadEast : RoadSouth), "Sloped end did not reorient to its first neighbor");

            auto bendWorld = RampWorld(alongX, descending);
            const auto bendEnd = tile(6, 10);
            const auto bend = bendWorld.PlaceRoads({a.x, a.z, bendEnd.x, bendEnd.z, alongX});
            Check(bend.changed == 4 && bend.evaluation.blocked == 3 && !bendWorld.GetRoadAt(p.x, p.z),
                  "A bend/cross-slope leg was placed on the incline");

            // A corner and later a four-way are still allowed on the upper/lower plateau.
            auto plateau = RampWorld(alongX, descending);
            const auto end = tile(8, 10), corner = tile(8, 8);
            Check(plateau.PlaceRoads({a.x, a.z, end.x, end.z, alongX}).changed == 9 &&
                  GetRoadVisual(*plateau.GetRoadAt(corner.x, corner.z)).shape == RoadShape::Curve,
                  "Plateau turn after an incline rejected");
            const auto branch1 = tile(9, 8), branch2 = tile(8, 7);
            Check(plateau.PlaceRoad(branch1.x, branch1.z) && plateau.PlaceRoad(branch2.x, branch2.z) &&
                  GetRoadVisual(*plateau.GetRoadAt(corner.x, corner.z)).shape == RoadShape::FourWay,
                  "Flat junction at ramp end rejected");
        }

        // Non-planar corners, a saddle, and a diagonal plane are all unsuitable;
        // even the diagonal plane has no level pair across either road end.
        for (const auto heights : {std::array<float, 4>{1, 1, 1, 2}, {1, 2, 2, 1}, {1, 2, 2, 3}, {0, -1, 0, -1}})
        {
            auto world = FlatWorld();
            auto& t = const_cast<TerrainHeightmap&>(world.GetTerrain());
            t.heights[6 * t.dimensions.CornerStride() + 6] = heights[0];
            t.heights[6 * t.dimensions.CornerStride() + 7] = heights[1];
            t.heights[7 * t.dimensions.CornerStride() + 6] = heights[2];
            t.heights[7 * t.dimensions.CornerStride() + 7] = heights[3];
            Check(!world.PlaceRoad(6, 6) && world.GetRoadAllowedConnections(6, 6) == 0,
                  "Uneven or submerged corner pairs accepted");
        }
        std::cout << "PASS: slopes in both axes/signs, shared edge heights, downhill ends, plateau junctions, blocked turns/crossings, parallel roads and uneven/wet pairs\n";
    }

    void CheckModularAssets(const std::filesystem::path& directory)
    {
        for (const auto& asset : RoadAssets)
        {
            if (asset.extendDiagonalApproaches || asset.fitGridTransition || asset.mixedJunction || asset.rotationOffset != 0) continue;
            const auto mesh = LoadStaticGlb(directory / std::filesystem::path(asset.model).filename(), 1.0f / MetersPerTile);
            std::uint8_t openings = 0;
            for (unsigned d = 0; d < 4; ++d)
            {
                float minimum = 100, maximum = -100;
                for (const auto& v : mesh.vertices)
                {
                    Check(std::abs(v.x) <= .5001f && std::abs(v.z) <= .5001f && Near(v.y, 0) && Near(v.ny, 1),
                          "Modular mesh escaped tile or lost Y-up");
                    const float axis = d % 2 ? v.z : v.x, across = d % 2 ? v.x : v.z;
                    if (Near(axis, d < 2 ? -.5f : .5f))
                    {
                        minimum = std::min(minimum, across); maximum = std::max(maximum, across);
                    }
                }
                if (minimum <= maximum)
                {
                    Check(Near(minimum, -.25f) && Near(maximum, .25f), "Mesh opening not aligned to its neighbor");
                    openings |= static_cast<std::uint8_t>(1 << d);
                }
            }
            Check(openings == asset.connections, "Asset definition ignores an authored node transform/opening");
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                const auto& a = mesh.vertices[mesh.indices[i]];
                const auto& b = mesh.vertices[mesh.indices[i + 1]];
                const auto& c = mesh.vertices[mesh.indices[i + 2]];
                Check((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) > 0,
                      "Modular road triangle not upward-facing");
            }
        }
        std::cout << "PASS: five real GLBs, authored transforms, matching edge widths/openings and backface-culling winding\n";
    }

    void CheckDiagonals()
    {
        const int dx[] = {-1, 1, 1, -1}, dz[] = {-1, -1, 1, 1};
        for (int d = 0; d < 4; ++d)
        {
            auto world = FlatWorld();
            ToolController controller(world);
            controller.SetTool(std::make_unique<RoadTool>());
            Check(controller.Begin(At(7, 7)), "Diagonal drag begin failed");
            const int ex = 7 + 4 * dx[d], ez = 7 + 4 * dz[d];
            controller.Update(At(ex, ez), .016f);
            const auto preview = controller.BuildPreview();
            const auto path = std::get<RoadRequest>(controller.End(At(ex, ez))).path;
            Check(path.diagonal && preview.roadPath == path && preview.placementAllowed,
                  "Diagonal preview/release mismatch");
            Check(preview.tiles->minX == std::min(7, ex) - RoadFootprintRadius && preview.tiles->maxZ == std::max(7, ez) + RoadFootprintRadius,
                  "Preview omitted the diagonal footprint");
            Check(world.PlaceRoads(path).changed == 5, "Diagonal route left holes");
            const auto forward = static_cast<std::uint8_t>(16 << d);
            const auto backward = RotateRoadConnections(forward, 2);
            const auto axis = static_cast<std::uint8_t>(forward | backward);
            Check(RotateRoadConnections(forward, 1) == (16 << ((d + 1) % 4)), "Diagonal bit rotation failed");
            for (int step = 0; step <= 4; ++step)
            {
                const int x = 7 + step * dx[d], z = 7 + step * dz[d];
                const auto* road = world.GetRoadAt(x, z);
                const auto mask = step == 0 ? forward : step == 4 ? backward : axis;
                Check(road && road->diagonalAxis == axis && road->connections == mask,
                      "Diagonal topology or endpoint orientation wrong");
                const auto visual = GetRoadVisual(*road);
                Check(visual.lengthScale == RoadDiagonalLength &&
                      visual.shape == (step == 0 || step == 4 ? RoadShape::DeadEnd : RoadShape::Straight),
                      "Diagonal did not reuse stretched straight/end meshes");
                const float angle = visual.quarterTurns * 1.57079632679f;
                if (step == 0 || step == 4)
                {
                    const int facing = step == 0 ? 1 : -1;
                    Check(Near(-std::cos(angle) * RoadDiagonalLength, static_cast<float>(facing * dx[d])) &&
                          Near(-std::sin(angle) * RoadDiagonalLength, static_cast<float>(facing * dz[d])),
                          "Diagonal dead end faces away from its neighbor");
                }
                for (const auto p : {ToolTile{x, z}, {x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}})
                    Check(world.GetTile(p.x, p.z).IsOccupied() && !CanZoneTile(world, p.x, p.z),
                          "Pavement footprint allowed zoning/building occupancy");
            }
            const auto revision = world.GetRoadRevision();
            const RoadPath reverse{ex, ez, 7, 7, true, true};
            const auto reused = world.PlaceRoads(reverse);
            Check(!reused.changed && reused.evaluation.unchanged == 5 && !reused.evaluation.blocked &&
                  revision == world.GetRoadRevision(), "Reverse drag duplicated a diagonal road");
            Check(world.PlaceRoads({ex, ez, ex + dx[d], ez + dz[d], true, true}).changed == 1 &&
                  world.GetRoadAt(ex, ez)->connections == axis, "Extending a diagonal did not replace the old end");
            Check(world.CheckRoadPlacement(7, 7, RoadWest) == RoadPlacementStatus::UnsupportedConnection,
                  "Unsupported diagonal/cardinal turn accepted");
        }

        Check(RoadPath::FromDrag(3,3,8,7)==RoadPath{3,3,8,7,true,false,true,false},
              "Near-diagonal drag did not reach the exact cursor tile");
        Check(RoadPath::FromDrag(3,3,9,6).mixed,"A 2:1 drag did not combine grid and diagonal legs");
        auto world = FlatWorld();
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x)
            world.GetTile(x, z).zoneType = ZoneType::ResidentialLight;
        const RoadPath diagonal{3, 3, 10, 10, true, true};
        // A building beside the centerline blocks both modules that would overlap it.
        world.GetTile(6, 5).buildingId = 1;
        auto result = world.PlaceRoads(diagonal);
        Check(result.changed == 2 && result.evaluation.blocked == 6 &&
              !world.GetRoadAt(5, 5) && !world.GetRoadAt(6, 6), "Diagonal footprint collision/partial placement failed");
        Check(world.GetRoadAt(4, 4)->connections == RoadNorthWest &&
              !world.GetRoadAt(7, 7), "Diagonal continued beyond a blocked gap");
        Check(world.GetTile(6, 5).buildingId == 1 && world.GetTile(6, 5).zoneType == ZoneType::ResidentialLight &&
              world.GetTile(4, 3).zoneType == ZoneType::None && world.GetTile(2, 2).zoneType == ZoneType::ResidentialLight,
              "Placement cleared an unrelated zone or retained a zone under its footprint");
        world.GetTile(6, 5).buildingId = NoBuilding;
        Check(world.PlaceRoads(diagonal).changed == 6 && world.GetRoadAt(4, 4)->connections == RoadDiagonalNWSE &&
              world.GetRoadAt(7, 7)->connections == RoadDiagonalNWSE, "Filling a gap did not join diagonal stretches");
        Check(!world.PlaceRoad(4, 3), "Cardinal road overwrote a diagonal shoulder reservation");
        Check(!world.PlaceRoads({2, 2, 4, 5, true, true}).evaluation.valid &&
              !world.PlaceRoads({2, 2, 2, 2, true, true}).evaluation.valid, "Malformed diagonal path accepted");

        auto edge = FlatWorld();
        result = edge.PlaceRoads({0, 0, 15, 15, true, true});
        Check(result.changed == 14 && result.evaluation.blocked == 2 && !edge.GetRoadAt(0, 0) && !edge.GetRoadAt(15, 15),
              "Diagonal pavement escaped the map edge");
        auto parallel = FlatWorld();
        Check(parallel.PlaceRoads({2, 2, 9, 9, true, true}).changed == 8 &&
              parallel.PlaceRoads({2, 6, 9, 13, true, true}).changed == 8, "Separate parallel diagonals rejected");
        Check(parallel.GetRoadAt(6, 6)->connections == RoadDiagonalNWSE &&
              parallel.GetRoadAt(6, 10)->connections == RoadDiagonalNWSE, "Parallel roads gained unwanted connections");

        for (float height : {-TerrainHeightStep, TerrainBaseElevation + TerrainHeightStep})
        {
            auto terrainWorld = FlatWorld();
            auto& t = const_cast<TerrainHeightmap&>(terrainWorld.GetTerrain());
            // This corner is outside the anchor tile but within its west shoulder.
            t.heights[6 * t.dimensions.CornerStride() + 5] = height;
            Check(terrainWorld.CheckRoadPlacement(6, 6, RoadDiagonalNWSE) != RoadPlacementStatus::Allowed,
                  "Diagonal ignored water/uneven ground under its shoulder");
        }
        auto slope = RampWorld(true, false);
        Check(slope.CheckRoadPlacement(6, 6, RoadDiagonalNWSE) == RoadPlacementStatus::NotFlat,
              "Diagonal was allowed on a slope");
        auto single = FlatWorld();
        Check(single.PlaceRoad(6, 6, RoadNorthWest) && single.GetRoadAt(6, 6)->connections == 0 &&
              GetRoadVisual(*single.GetRoadAt(6, 6)).quarterTurns == .5f,
              "Isolated diagonal lost its axis");
        std::cout << "PASS: diagonal dragging/topology, both axes and endpoint directions, footprint collisions, zones, gap joining, map edges and flat-only rules\n";
    }

    void CheckDiagonalGeometry(const std::filesystem::path& directory)
    {
        // Use the supplied GLBs, not just ideal rectangles: their authored ends
        // and openings must fit the reserved footprint and meet their neighbors.
        const auto transform = [](float x, float z, float turns)
        {
            const float angle = turns * 1.57079632679f;
            x *= RoadDiagonalLength;
            return std::pair{x * std::cos(angle) - z * std::sin(angle),
                             x * std::sin(angle) + z * std::cos(angle)};
        };
        for (auto shape : {RoadShape::Straight, RoadShape::DeadEnd})
        {
            const auto& asset = RoadAssets[static_cast<std::size_t>(shape)];
            const auto mesh = LoadStaticGlb(directory / std::filesystem::path(asset.model).filename(), 1.0f / MetersPerTile);
            for (float turns : {.5f, 1.5f, 2.5f, 3.5f})
            {
                for (const auto& v : mesh.vertices)
                {
                    const auto [x, z] = transform(v.x, v.z, turns);
                    // These linear bounds also hold throughout every triangle,
                    // excluding all four diagonal-neighbor tiles from the mesh.
                    Check(std::abs(x + z) <= 1.00001f && std::abs(x - z) <= 1.00001f &&
                          std::abs(x) < 1 && std::abs(z) < 1, "Diagonal mesh escaped its five-tile footprint");
                }
                const auto a = transform(-.5f, -.25f, turns), b = transform(-.5f, .25f, turns);
                Check(Near(std::hypot(b.first - a.first, b.second - a.second), .5f),
                      "Diagonal stretched road width");
                const auto offset = transform(-1, 0, turns);
                // A neighbor's opposite opening, translated one diagonal step.
                const auto na = transform(.5f, -.25f, turns), nb = transform(.5f, .25f, turns);
                Check(Near(a.first, offset.first + na.first) && Near(a.second, offset.second + na.second) &&
                      Near(b.first, offset.first + nb.first) && Near(b.second, offset.second + nb.second),
                      "Diagonal mesh openings left a gap or overlap");
            }
        }
        std::cout << "PASS: supplied straight/end geometry fits diagonal reservations, preserves width and joins without gaps\n";
    }

    void CheckDiagonalJunctions()
    {
        for (bool firstNWSE : {false, true}) for (int branchSign : {-1, 1}) for (bool reverse : {false, true})
        {
            auto world = FlatWorld();
            const RoadPath first = firstNWSE ? RoadPath{3, 3, 11, 11, true, true} : RoadPath{3, 11, 11, 3, true, true};
            Check(world.PlaceRoads(first).changed == 9, "Initial diagonal junction road failed");
            const auto original = *world.GetRoadAt(7, 7);
            const int dx = branchSign, dz = firstNWSE ? -branchSign : branchSign;
            RoadPath branch{7, 7, 7 + 4 * dx, 7 + 4 * dz, true, true};
            if (reverse) { std::swap(branch.startX, branch.endX); std::swap(branch.startZ, branch.endZ); }
            const auto planned = world.PlanRoads(branch);
            Check(planned.evaluation.changes == 5 && planned.evaluation.upgrades == 1 && !planned.evaluation.blocked,
                  "T junction preview failed to plan its existing center upgrade");
            const auto placed = world.PlaceRoads(branch);
            Check(placed.changed == 5 && world.GetRoads().size() == 13, "T junction duplicated or skipped the center");
            const auto* junction = world.GetRoadAt(7, 7);
            const auto toward = dx < 0 ? (dz < 0 ? RoadNorthWest : RoadSouthWest) : (dz < 0 ? RoadNorthEast : RoadSouthEast);
            Check(junction->id == original.id && junction->connections == (original.connections | toward) &&
                  junction->diagonalAxis == RoadAllDiagonals && GetRoadVisual(*junction).shape == RoadShape::DiagonalThreeWay,
                  "T junction lost its ID, arms, or mesh selection");
            const auto revision = world.GetRoadRevision();
            const auto repeat = world.PlaceRoads(branch);
            Check(!repeat.changed && repeat.evaluation.unchanged == 5 && !repeat.evaluation.blocked &&
                  revision == world.GetRoadRevision(), "Repeated T drag mutated a road");
            const auto fourth = world.PlaceRoads({7, 7, 7 - 4 * dx, 7 - 4 * dz, true, true});
            junction = world.GetRoadAt(7, 7);
            Check(fourth.changed == 5 && fourth.evaluation.upgrades == 1 && junction->id == original.id &&
                  junction->connections == RoadAllDiagonals && GetRoadVisual(*junction).shape == RoadShape::DiagonalFourWay,
                  "T junction did not upgrade to a cross");
            for (const auto& road : world.GetRoads())
            {
                Check(SupportedDiagonalConnections(road.connections), "Junction produced an unsupported neighbor shape");
                if (road.tileX != 7 || road.tileZ != 7)
                    Check(GetRoadVisual(road).shape == RoadShape::Straight || GetRoadVisual(road).shape == RoadShape::DeadEnd,
                          "Approach turned into an unintended junction");
            }
        }
        for (bool reverse : {false, true})
        {
            auto world = FlatWorld();
            world.PlaceRoads({3, 3, 11, 11, true, true});
            const RoadPath cross = reverse ? RoadPath{11, 3, 3, 11, true, true} : RoadPath{3, 11, 11, 3, true, true};
            const auto placed = world.PlaceRoads(cross);
            Check(placed.changed == 9 && placed.evaluation.upgrades == 1 && world.GetRoads().size() == 17 &&
                  world.GetRoadAt(7, 7)->connections == RoadAllDiagonals, "One drag failed to create a diagonal cross");
        }

        // A junction can also fill a missing center between existing arms.
        auto gap = FlatWorld();
        gap.PlaceRoads({3, 3, 6, 6, true, true}); gap.PlaceRoads({8, 8, 11, 11, true, true});
        Check(gap.PlaceRoads({3, 11, 11, 3, true, true}).changed == 9 &&
              gap.GetRoadAt(7, 7)->connections == RoadAllDiagonals, "Missing-center junction failed");

        // A perpendicular road ending at an existing dead end needs a bend,
        // whereas a road passing through that end can form a supported T.
        auto ends = FlatWorld();
        ends.PlaceRoads({3, 3, 7, 7, true, true});
        const auto original = *ends.GetRoadAt(7, 7);
        const auto turn = ends.PlaceRoads({3, 11, 7, 7, true, true});
        Check(!turn.evaluation.blocked && turn.changed == 5 && turn.evaluation.upgrades == 1 &&
              ends.GetRoadAt(7, 7)->id == original.id &&
              GetRoadVisual(*ends.GetRoadAt(7, 7)).shape == RoadShape::DiagonalCurve,
              "Two-arm diagonal bend failed");
        Check(ends.PlaceRoads({3, 11, 11, 3, true, true}).evaluation.upgrades == 1 &&
              GetRoadVisual(*ends.GetRoadAt(7, 7)).shape == RoadShape::DiagonalThreeWay,
              "Passing through a dead end did not form a T");

        auto partial = FlatWorld();
        partial.PlaceRoads({3, 3, 7, 7, true, true});
        partial.GetTile(8, 6).buildingId = 42;
        const auto planned = partial.PlanRoads({3, 11, 11, 3, true, true});
        Check(planned.tiles[4].status == RoadPlacementStatus::UpgradeRoad,
              "Surviving two-arm corner was not planned as a curve");
        const auto built = partial.PlaceRoads({3, 11, 11, 3, true, true});
        Check(built.changed == planned.evaluation.changes &&
              GetRoadVisual(*partial.GetRoadAt(7, 7)).shape == RoadShape::DiagonalCurve && partial.GetTile(8, 6).buildingId == 42,
              "Commit disagreed with its blocked-approach plan");

        auto halfTile = FlatWorld();
        halfTile.PlaceRoads({2, 2, 12, 12, true, true});
        const auto before = *halfTile.GetRoadAt(7, 7);
        const auto halfCross = halfTile.PlaceRoads({3, 12, 12, 3, true, true});
        Check(halfCross.evaluation.blocked > 0 && halfTile.GetRoadAt(7, 7)->connections == before.connections &&
              halfTile.GetRoadAt(7, 7)->diagonalAxis == before.diagonalAxis,
              "A between-tile crossing modified the existing road");
        auto cardinal = FlatWorld();
        cardinal.PlaceRoads({2, 7, 12, 7});
        const auto mixed = cardinal.PlaceRoads({3, 3, 11, 11, true, true});
        Check(mixed.evaluation.blocked == 0 && cardinal.GetRoadAt(7, 7)->mixedConnections ==
              (RoadWest | RoadEast | RoadNorthWest | RoadSouthEast), "Mixed intersection failed");
        std::cout << "PASS: all diagonal T orientations, cross/extension/gap upgrades, stable IDs, reuse, partial approaches, curves and rejected half-tile crossings\n";
    }

    void CheckDiagonalJunctionGeometry(const std::filesystem::path& directory)
    {
        for (auto shape : {RoadShape::DiagonalThreeWay, RoadShape::DiagonalFourWay})
        {
            const auto& asset = RoadAssets[static_cast<std::size_t>(shape)];
            const auto path = directory / std::filesystem::path(asset.model).filename();
            const auto source = LoadStaticGlb(path, 1.0f / MetersPerTile);
            const auto mesh = LoadRoadMesh(path, asset);
            Check(mesh.vertices.size() == source.vertices.size() && mesh.indices == source.indices,
                  "Junction adaptation replaced authored topology");
            unsigned mask = 0;
            for (unsigned d = 0; d < 4; ++d)
            {
                float minimum = 100, maximum = -100;
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                {
                    const auto& v = mesh.vertices[i]; const auto& old = source.vertices[i];
                    Check(std::abs(v.x) <= RoadDiagonalLength * .5f + .00001f &&
                          std::abs(v.z) <= RoadDiagonalLength * .5f + .00001f && Near(v.ny, 1) && Near(v.y, 0),
                          "Junction escaped its rotated cell or changed surface/normal");
                    if (std::abs(old.x) < .25001f && std::abs(old.z) < .25001f)
                        Check(Near(old.x, v.x) && Near(old.z, v.z), "Junction center was scaled");
                    Check(Near(v.u, old.u) && Near(v.v, old.v), "Junction adaptation lost its texture mapping");
                    const float axis = d % 2 ? v.z : v.x, across = d % 2 ? v.x : v.z;
                    if (Near(axis, (d < 2 ? -.5f : .5f) * RoadDiagonalLength))
                    { minimum = std::min(minimum, across); maximum = std::max(maximum, across); }
                }
                if (minimum <= maximum)
                {
                    Check(Near(minimum, -.25f) && Near(maximum, .25f), "Diagonal junction opening lost its eight-meter width");
                    mask |= 16 << d;
                }
            }
            Check(mask == asset.connections, "Diagonal junction openings disagree with its mask");
            for (unsigned turn = 0; turn < 4; ++turn)
            {
                const auto rotated = RotateRoadConnections(static_cast<std::uint8_t>(mask), turn);
                const auto visual = GetRoadVisual(rotated);
                Check(visual.shape == shape && visual.lengthScale == 1 &&
                      RotateRoadConnections(asset.connections, static_cast<unsigned>(visual.quarterTurns)) == rotated &&
                      visual.quarterTurns - std::floor(visual.quarterTurns) == .5f,
                      "Junction visual is stretched twice or faces the wrong directions");
            }
        }
        std::cout << "PASS: real junction GLBs, unchanged centers/UVs, eight-meter openings and diagonal seam positions\n";
    }

    void CheckMixedNetworks()
    {
        // Reproducible combinations of partial routes, close junctions and both
        // drag directions catch topology errors beyond isolated T/cross fixtures.
        for (std::uint32_t seed = 1; seed <= 8; ++seed)
        {
            auto world = FlatWorld();
            auto random = seed;
            const auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
            world.GetTile(5, 6).buildingId = 1;
            world.GetTile(10, 9).infrastructureId = 99;
            for (int edit = 0; edit < 64; ++edit)
            {
                const int x = 1 + static_cast<int>(next() % 14), z = 1 + static_cast<int>(next() % 14);
                const int dx = (next() & 1) ? 1 : -1, dz = (next() & 1) ? 1 : -1;
                const int available = std::min(dx > 0 ? 14 - x : x - 1, dz > 0 ? 14 - z : z - 1);
                if (!available) continue;
                const int steps = 1 + static_cast<int>(next() % available);
                RoadPath path{x, z, x + dx * steps, z + dz * steps, true, true};
                if (edit % 5 == 0) { path.diagonal = false; path.endZ = z; }
                if (edit % 3 == 0 && steps>=2)
                    path=world.PlanRoadDrag(x,z,x+dx*steps,z+dz*(steps/2)).path;
                auto backward = world;
                auto reverse = path;
                std::swap(reverse.startX, reverse.endX); std::swap(reverse.startZ, reverse.endZ);
                if (reverse.mixed) reverse.diagonalFirst=!reverse.diagonalFirst;
                else if (!reverse.diagonal) reverse.xFirst=!reverse.xFirst;
                const auto plan = world.PlanRoads(path);
                const auto reversePlan = backward.PlanRoads(reverse);
                const auto placed = world.PlaceRoads(path), reversed = backward.PlaceRoads(reverse);
                Check(placed.changed == plan.evaluation.changes && reversed.changed == reversePlan.evaluation.changes,
                      "Network plan/commit counts disagree");
                for (const auto* p : {&plan,&reversePlan})
                {
                    int sections=0;
                    for (std::size_t i=0;i<p->tiles.size();++i)
                        if (RoadPlacementAccepted(p->tiles[i].status) &&
                            (i==0 || !RoadPlacementAccepted(p->tiles[i-1].status))) ++sections;
                    Check(sections<=1,"Network plan retained multiple valid sections");
                }
                // Fully usable paths remain symmetric. With obstacles, reversing
                // intentionally selects a different section nearest the new start.
                if (!plan.evaluation.blocked && !reversePlan.evaluation.blocked)
                for (int pz = 0; pz < 16; ++pz) for (int px = 0; px < 16; ++px)
                {
                    const auto* a = world.GetRoadAt(px, pz); const auto* b = backward.GetRoadAt(px, pz);
                    Check((a != nullptr) == (b != nullptr) && (!a ||
                          (a->connections == b->connections && a->diagonalAxis == b->diagonalAxis &&
                           a->mixedConnections == b->mixedConnections && a->suppressedConnections == b->suppressedConnections)),
                          "Reversing a drag changed the road topology");
                }
                for (const auto* state : {&world,&backward}) for (const auto& road : state->GetRoads())
                {
                    if (road.diagonalAxis) Check(SupportedDiagonalConnections(road.connections),
                        "A neighboring network edit created an unsupported diagonal bend");
                    if (road.mixedConnections) Check(SupportedMixedConnections(road.connections) &&
                        road.connections == road.mixedConnections, "Mixed junction lost an approach");
                    for (const auto& other : state->GetRoads()) if (road.id < other.id)
                        Check(!RoadMeshesOverlap(road, other), "Network edit created overlapping pavement");
                    constexpr int nx[] = {-1, 0, 1, 0, -1, 1, 1, -1}, nz[] = {0, -1, 0, 1, -1, -1, 1, 1};
                    for (int i = 0; i < 8; ++i) if (road.connections & (1 << i))
                    {
                        const auto* neighbor = state->GetRoadAt(road.tileX + nx[i], road.tileZ + nz[i]);
                        Check(neighbor && (neighbor->connections & RotateRoadConnections(static_cast<std::uint8_t>(1 << i), 2)),
                              "A road connection is not reciprocal");
                    }
                }
            }
        }
        std::cout << "PASS: mixed-network plans match commits, partial drags keep one section, and both drag directions preserve reciprocal/supported connections\n";
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
        CheckConnectionsAndRoutes();
        CheckMixedDrags();
        CheckFirstRoadSection();
        CheckParallelRoads();
        CheckSlopes();
        CheckDiagonals();
        CheckDiagonalJunctions();
        CheckMixedNetworks();
        CheckImporter(argv[1], argv[2]);
        CheckModularAssets(std::filesystem::path(argv[1]).parent_path());
        CheckDiagonalGeometry(std::filesystem::path(argv[1]).parent_path());
        CheckDiagonalJunctionGeometry(std::filesystem::path(argv[1]).parent_path());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
