#include "world_zoning.hpp"
#include "world.hpp"
#include "city.hpp"
#include "zone_render_data.hpp"
#include "zone_tool.hpp"
#include "toolcontroller.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    World FlatWorld(unsigned int count)
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(count);
        settings.hillHeight = 0;
        settings.lakes.count = 0;
        return World(settings);
    }
    ToolPointer At(int x, int z)
    {
        return {100, 100, ToolHit{{x, z}, x + 0.5f, TerrainBaseElevation, z + 0.5f}};
    }

    void CheckPlacement()
    {
        World world = FlatWorld(16);
        City city;
        city.funds = 10000;
        const auto type = ZoneType::ResidentialLight;
        const auto price = Zone::GetZoneDefinition(type).costPerTile;
        ToolController controller(world);
        controller.SetTool(std::make_unique<ZoneTool>(type));
        Check(std::holds_alternative<std::monostate>(controller.End(At(2, 2))), "Flyout release created a zone");
        controller.Begin(At(2, 2));
        const auto single = controller.End(At(2, 2));
        const auto placed = ApplyZoning(world, city, std::get<ZoneRequest>(single));
        Check(placed.changed == 1 && city.funds == 10000 - price && world.GetTile(2, 2).zoneType == type,
              "Single-click placement/cost failed");
        Check(ApplyZoning(world, city, {{2, 2, 2, 2}, type}).changed == 0 && city.funds == 10000 - price,
              "Repeated zoning charged again");

        world.GetTile(3, 3).buildingId = 7;
        world.GetTile(4, 3).infrastructureId = 11;
        controller.Begin(At(5, 5));
        controller.Update(At(2, 2), 0.016f);
        const auto preview = controller.BuildPreview();
        const ZoneRequest rectangle{*preview.tiles, *preview.zoneType};
        const auto quote = EvaluateZoning(world, rectangle, city.funds);
        Check(quote.valid && quote.affordable && quote.changes == 13 && quote.blocked == 2 && quote.unchanged == 1 &&
              quote.cost == 13 * price, "Rectangle quote counted ineligible/unchanged tiles");
        const auto committed = ApplyZoning(world, city, std::get<ZoneRequest>(controller.End(At(2, 2))));
        Check(committed.changed == quote.changes && city.funds == 10000 - 14 * price,
              "Preview and commit prices differ");
        Check(world.GetTile(3, 3).zoneType == ZoneType::None && world.GetTile(4, 3).zoneType == ZoneType::None,
              "Occupied tiles were zoned");
        world.GetTile(3, 3).zoneType = type; // An existing developed zone also cannot change.
        const ZoneRequest replacement{{2, 2, 5, 5}, ZoneType::CommercialMedium};
        city.funds = 1;
        const auto before = world.GetTile(2, 2).zoneType;
        const auto failed = ApplyZoning(world, city, replacement);
        Check(!failed.evaluation.affordable && failed.changed == 0 && city.funds == 1 &&
              world.GetTile(2, 2).zoneType == before, "Insufficient funds partially committed");
        city.funds = EvaluateZoning(world, replacement, 100000).cost;
        Check(ApplyZoning(world, city, replacement).changed == 14 && city.funds == 0 &&
              world.GetTile(3, 3).zoneType == type, "Exact-funds replacement/occupancy rule failed");
        Check(ApplyZoning(world, city, {{2, 2, 5, 5}, ZoneType::None}).changed == 14 && city.funds == 0 &&
              world.GetTile(3, 3).zoneType == type, "Dezoning removed occupied tiles or cost funds");

        controller.Begin(At(0, 0));
        controller.Update(At(5, 5), 0.016f);
        controller.Cancel();
        Check(std::holds_alternative<std::monostate>(controller.End(At(5, 5))) && world.GetTile(0, 0).zoneType == ZoneType::None,
              "Canceled drag committed");
        controller.Begin(At(0, 0));
        Check(std::holds_alternative<std::monostate>(controller.End({})), "UI/off-map release committed");

        const ZoneRequest invalid[] = {{{-1, 0, 5, 5}, type}, {{0, 0, 16, 16}, type}, {{5, 5, 0, 0}, type},
                                       {{0, 0, 0, 0}, ZoneType::Count}};
        for (const auto& request : invalid)
            Check(!ApplyZoning(world, city, request).evaluation.valid && city.funds == 0, "Invalid request modified funds");
        std::cout << "PASS: click/drag, changed-only pricing, occupancy, rezone, dezone, budget atomicity, and cancellation\n";
    }

    void CheckTerrainRules()
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(128);
        World world(settings);
        unsigned int flat = 0, slopes = 0, water = 0;
        const auto& heights = world.GetTerrain();
        for (unsigned int z = 0; z < 128; ++z)
            for (unsigned int x = 0; x < 128; ++x)
            {
                const float a = heights.Height(x, z), b = heights.Height(x + 1, z),
                            c = heights.Height(x, z + 1), d = heights.Height(x + 1, z + 1);
                const bool expected = a == b && a == c && a == d && a >= TerrainSeaLevel;
                Check(CanZoneTile(world, x, z) == expected, "Flat/dry rule disagrees with four corner heights");
                if (expected) ++flat;
                else if (std::min({a, b, c, d}) < TerrainSeaLevel) ++water;
                else ++slopes;
            }
        Check(flat && slopes && water, "Terrain fixture did not exercise all restrictions");
        City city;
        city.funds = 10000000;
        const ZoneRequest request{{0, 0, 127, 127}, ZoneType::IndustrialFarm};
        const auto result = ApplyZoning(world, city, request);
        Check(result.changed == flat && result.evaluation.blocked == slopes + water,
              "Rectangle did not skip slopes/water");
        std::cout << "PASS: terrain rules across " << flat << " flat, " << slopes << " sloped, and " << water << " water tiles\n";
    }

    void CheckRenderingData()
    {
        World world = FlatWorld(4);
        const auto base = BuildMinimapImage(BuildTerrain(world.GetTerrain()));
        City city;
        city.funds = 10000;
        const auto basePatch = BuildZoneMinimapPatch(base, world, {0, 0, 3, 3});
        Check(basePatch.rgba == base.rgba, "Unzoned minimap changed terrain colors");
        ApplyZoning(world, city, {{1, 1, 1, 1}, ZoneType::ResidentialMixedUse});
        world.GetTile(2, 1).buildingId = 1;
        const auto pixels = BuildZonePixels(world, {1, 1, 2, 1});
        Check(pixels.size() == 8 && pixels[3] == 2 * static_cast<unsigned int>(ZoneType::ResidentialMixedUse) + 1 &&
              pixels[7] == 0, "Zone type/eligibility texture metadata incorrect");
        const auto colored = BuildZoneMinimapPatch(base, world, {1, 1, 1, 1});
        Check(colored.width == 2 && colored.height == 2 && colored.rgba[3] == 255,
              "Wrong minimap region shape/alpha");
        const auto original = (static_cast<std::size_t>(2) * base.width + 2) * 4;
        Check(!std::equal(colored.rgba.begin(), colored.rgba.begin() + 3, base.rgba.begin() + original), "Zone was not colored on minimap");
        ApplyZoning(world, city, {{1, 1, 1, 1}, ZoneType::None});
        const auto restored = BuildZoneMinimapPatch(base, world, {1, 1, 1, 1});
        Check(std::equal(restored.rgba.begin(), restored.rgba.begin() + 4, base.rgba.begin() + original),
              "Dezone did not restore original minimap color");

        for (unsigned int n : {128u, 256u, 512u, 1024u})
        {
            World sized = FlatWorld(n);
            City largeCity;
            largeCity.funds = std::numeric_limits<std::int64_t>::max();
            const int last = static_cast<int>(n) - 1;
            Check(ApplyZoning(sized, largeCity, {{last, last, last, last}, ZoneType::Seaport}).changed == 1,
                  "Far-corner zone placement failed");
            const auto region = BuildZonePixels(sized, {last, last, last, last});
            Check(region.size() == 4 && region[3] == 2 * static_cast<unsigned int>(ZoneType::Seaport) + 1,
                  "Zone texture region stride failed on large map");
        }
        std::cout << "PASS: overlay/minimap data, dezone restoration, and all four map sizes\n";
    }
}

int main()
{
    try { CheckPlacement(); CheckTerrainRules(); CheckRenderingData(); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
