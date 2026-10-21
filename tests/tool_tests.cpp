#include "toolcontroller.hpp"
#include "world.hpp"
#include "zone_tool.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Empty(const ToolResult& result)
    {
        return std::holds_alternative<std::monostate>(result);
    }

    // Synthetic, already-picked hits: these tests do not implement picking.
    ToolPointer At(int x, int z)
    {
        return {100.0f, 100.0f, ToolHit{{x, z}, x + 0.5f, 1.0f, z + 0.5f}};
    }

    void CheckRequest(const ToolResult& result, ToolTileRect bounds, ZoneType type)
    {
        const auto* request = std::get_if<ZoneRequest>(&result);
        Check(request && request->tiles == bounds && request->zoneType == type,
              "Wrong zone request");
    }

    void CheckZoneSelection(World& world)
    {
        ToolController controller(world);
        Check(!controller.HasTool() && !controller.Begin(At(0, 0)) &&
              Empty(controller.End(At(0, 0))) && Empty(controller.Update(At(0, 0), 0.016f)),
              "An empty controller accepted an interaction");
        controller.SetTool(std::make_unique<ZoneTool>(ZoneType::ResidentialLight));
        Check(controller.HasTool() && !controller.IsInteracting(), "Tool selection started a gesture");

        // A flyout release, or hovering alone, must never create an edit.
        Check(Empty(controller.Update(At(4, 6), 0.016f)), "Hover requested an edit");
        auto preview = controller.BuildPreview();
        Check(preview.tiles == ToolTileRect{4, 6, 4, 6} &&
              preview.zoneType == ZoneType::ResidentialLight, "Missing one-tile hover preview");
        Check(Empty(controller.End(At(4, 6))), "Release without press requested an edit");

        const std::pair<ToolTile, ToolTile> drags[] = {
            {{2, 3}, {8, 9}}, {{8, 9}, {2, 3}},
            {{2, 9}, {8, 3}}, {{8, 3}, {2, 9}}
        };
        for (const auto& [first, last] : drags)
        {
            Check(controller.Begin(At(first.x, first.z)), "Valid drag was rejected");
            Check(!controller.Begin(At(0, 0)), "A second press replaced the drag anchor");
            Check(Empty(controller.Update(At(last.x, last.z), 0.016f)), "Drag applied before release");
            Check(controller.BuildPreview().tiles == ToolTileRect{2, 3, 8, 9}, "Wrong rectangle preview");
            CheckRequest(controller.End(At(last.x, last.z)), {2, 3, 8, 9}, ZoneType::ResidentialLight);
            Check(!controller.IsInteracting() && !controller.BuildPreview().tiles,
                  "Completed drag retained pending state");
            Check(Empty(controller.End(At(last.x, last.z))), "Double release duplicated the request");
        }

        // The release coordinate is authoritative, even without an Update.
        Check(controller.Begin(At(0, 0)), "Map-edge drag was rejected");
        CheckRequest(controller.End(At(15, 15)), {0, 0, 15, 15}, ZoneType::ResidentialLight);
        Check(controller.Begin(At(15, 15)), "Last map tile was rejected");
        CheckRequest(controller.End(At(15, 15)), {15, 15, 15, 15}, ZoneType::ResidentialLight);

        Check(controller.Begin(At(2, 3)), "Unable to begin cancellation test");
        controller.Update({}, 0.016f);
        Check(controller.IsInteracting() && !controller.BuildPreview().tiles,
              "Leaving terrain should hide preview while retaining the anchor");
        controller.Update(At(8, 9), 0.016f);
        Check(controller.BuildPreview().tiles == ToolTileRect{2, 3, 8, 9}, "Reentry lost the anchor");
        Check(Empty(controller.End({})) && !controller.IsInteracting() && !controller.BuildPreview().tiles,
              "Off-map/UI release committed a stale selection");

        controller.Begin(At(2, 3));
        controller.Cancel();
        Check(controller.HasTool() && !controller.IsInteracting() && !controller.BuildPreview().tiles &&
              Empty(controller.End(At(8, 9))), "Cancel did not discard the pending gesture");
        controller.Begin(At(2, 3));
        controller.SetTool(std::make_unique<ZoneTool>(ZoneType::None));
        Check(Empty(controller.End(At(8, 9))), "Switching tool inherited the old press");
        controller.Begin(At(4, 4));
        CheckRequest(controller.End(At(5, 5)), {4, 4, 5, 5}, ZoneType::None);

        // Requests and previews must leave authoritative World data unchanged.
        Check(std::all_of(world.GetTiles().begin(), world.GetTiles().end(),
                          [](const Tile& tile) { return tile.zoneType == ZoneType::None; }),
              "Tool modified World before placement validation/application");
        controller.ClearTool();
        Check(!controller.HasTool() && !controller.BuildPreview().tiles, "ClearTool left a preview");
    }

    void CheckInvalidHits(World& world)
    {
        ToolController controller(world);
        controller.SetTool(std::make_unique<ZoneTool>(ZoneType::CommercialDense));
        ToolPointer invalid[] = {{}, At(-1, 0), At(0, -1), At(16, 0), At(0, 16), At(1, 1), At(1, 1)};
        invalid[5].hit->worldY = std::numeric_limits<float>::quiet_NaN();
        invalid[6].screenX = std::numeric_limits<float>::infinity();
        for (const auto& pointer : invalid)
        {
            Check(!controller.Begin(pointer), "Invalid hit began a gesture");
            controller.Update(pointer, 0.016f);
            Check(!controller.BuildPreview().tiles, "Invalid hit produced a preview");
            controller.Begin(At(1, 1));
            Check(Empty(controller.End(pointer)) && !controller.IsInteracting(), "Invalid release requested an edit");
        }

        bool rejected = false;
        try { ZoneTool unknown(static_cast<ZoneType>(255)); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Unknown zone type was accepted");
    }

    struct Lifecycle
    {
        bool active = false;
        bool destroyed = false;
        bool destroyedWhileActive = false;
        float elapsed = -1.0f;
    };

    class TrackingTool final : public Tool
    {
    public:
        explicit TrackingTool(Lifecycle& state) : state(state) {}
        ~TrackingTool() override
        {
            state.destroyedWhileActive = state.active;
            state.destroyed = true;
        }
        bool Begin(const ToolContext&, const ToolPointer&) override { state.active = true; return true; }
        ToolResult Update(const ToolContext&, const ToolPointer&, float elapsed) override
        {
            state.elapsed = elapsed;
            return {};
        }
        ToolResult End(const ToolContext&, const ToolPointer&) override { state.active = false; return {}; }
        void Cancel() noexcept override { state.active = false; }
        void BuildPreview(ToolPreview&) const override {}
    private:
        Lifecycle& state;
    };

    void CheckLifetime(World& world)
    {
        Lifecycle replaced, cleared, destroyed;
        {
            ToolController controller(world);
            controller.SetTool(std::make_unique<TrackingTool>(replaced));
            controller.Begin(At(1, 1));
            controller.Update(At(1, 1), 0.25f);
            Check(replaced.elapsed == 0.25f, "Valid elapsed time was changed");
            for (float bad : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            {
                controller.Update(At(1, 1), bad);
                Check(replaced.elapsed == 0.0f, "Invalid brush time reached the tool");
            }
            controller.SetTool(std::make_unique<TrackingTool>(cleared));
            Check(replaced.destroyed && !replaced.destroyedWhileActive, "Replacement did not cancel before destruction");
            controller.Begin(At(1, 1));
            controller.ClearTool();
            Check(cleared.destroyed && !cleared.destroyedWhileActive, "Clear did not cancel before destruction");
            controller.SetTool(std::make_unique<TrackingTool>(destroyed));
            controller.Begin(At(1, 1));
        }
        Check(destroyed.destroyed && !destroyed.destroyedWhileActive, "Controller destruction did not cancel the gesture");
    }
}

int main()
{
    try
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(16);
        settings.lakes.count = 0;
        World world(settings);
        CheckZoneSelection(world);
        CheckInvalidHits(world);
        CheckLifetime(world);
        std::cout << "PASS: tool lifetime, zone requests/previews, drag directions, bounds, cancellation, and unchanged World\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
