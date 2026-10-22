#include "tool_sound_player.hpp"
#include "sound.hpp"
#include "road_tool.hpp"
#include "zone_tool.hpp"
#include "toolcontroller.hpp"
#include "world.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    std::vector<const SoundEffect*> played;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

// Record which existing instance plays, without opening an audio device.
SoundEffect::~SoundEffect() = default;
void SoundEffect::Play() { played.push_back(this); }

int main()
{
    try
    {
        SoundEffect zoneSound, roadSound, deniedSound;
        ToolSoundPlayer player;
        player.Play(ToolOutcome::Success, ToolSuccessSound::BuildRoad);
        player.Play(ToolOutcome::Rejected, ToolSuccessSound::PlaceZone);
        Check(played.empty(), "Unbound feedback played");

        player.Bind(ToolSuccessSound::PlaceZone, zoneSound);
        player.Bind(ToolSuccessSound::BuildRoad, roadSound);
        player.SetDeniedSound(deniedSound);

        RoadTool road;
        ZoneTool zone(ZoneType::ResidentialLight);
        ZoneTool dezone(ZoneType::None);
        ZoneTool alternative(ZoneType::Landfill, ToolSuccessSound::BuildRoad);
        RoadTool silent(ToolSuccessSound::None);
        Check(road.GetSuccessSound() == ToolSuccessSound::BuildRoad &&
              zone.GetSuccessSound() == ToolSuccessSound::PlaceZone &&
              dezone.GetSuccessSound() == ToolSuccessSound::PlaceZone &&
              alternative.GetSuccessSound() == ToolSuccessSound::BuildRoad &&
              silent.GetSuccessSound() == ToolSuccessSound::None, "Tool defaults/overrides incorrect");
        Check(played.empty(), "Constructing a tool played its success sound");

        player.Play(ToolOutcome::Success, road.GetSuccessSound());
        player.Play(ToolOutcome::PartialSuccess, zone.GetSuccessSound());
        player.Play(ToolOutcome::Success, alternative.GetSuccessSound());
        player.Play(ToolOutcome::Success, dezone.GetSuccessSound());
        player.Play(ToolOutcome::Rejected, road.GetSuccessSound());
        player.Play(ToolOutcome::Rejected, silent.GetSuccessSound());
        Check(played == std::vector<const SoundEffect*>{&roadSound, &zoneSound, &roadSound, &zoneSound,
                                                      &deniedSound, &deniedSound},
              "Success/partial/rejection mapping or shared effect reuse incorrect");

        const auto count = played.size();
        player.Play(ToolOutcome::NoChange, zone.GetSuccessSound());
        player.Play(ToolOutcome::Success, silent.GetSuccessSound());
        player.Play(ToolOutcome::PartialSuccess, silent.GetSuccessSound());
        player.Play(ToolOutcome::Success, ToolSuccessSound::Count);
        player.Play(ToolOutcome::Success, static_cast<ToolSuccessSound>(999));
        Check(played.size() == count, "No-op, silent or invalid sound selection played");
        for (const auto id : {ToolSuccessSound::None, ToolSuccessSound::Count, static_cast<ToolSuccessSound>(999)})
        {
            bool rejected = false;
            try { player.Bind(id, roadSound); } catch (const std::invalid_argument&) { rejected = true; }
            Check(rejected, "Invalid sound binding accepted");
        }

        TerrainSettings settings;
        settings.dimensions = MapDimensions(16); settings.hillHeight = 0; settings.lakes.count = 0;
        World world(settings);
        ToolController controller(world);
        Check(controller.GetSuccessSound() == ToolSuccessSound::None, "Empty controller has a sound");
        controller.SetTool(std::make_unique<RoadTool>());
        const auto sourceSound = controller.GetSuccessSound();
        controller.SetTool(std::make_unique<ZoneTool>(ZoneType::CommercialLight));
        Check(controller.GetSuccessSound() == ToolSuccessSound::PlaceZone && played.size() == count,
              "Tool selection changed playback or reported wrong sound");
        // Application passes the originating tool's ID along with its request.
        player.Play(ToolOutcome::Success, sourceSound);
        Check(played.size() == count + 1 && played.back() == &roadSound, "Feedback used a later tool's sound");
        controller.ClearTool();
        Check(controller.GetSuccessSound() == ToolSuccessSound::None, "Cleared controller retained sound");

        std::cout << "PASS: sound defaults/overrides, shared effects, success/partial/rejection/no-op, source ID and invalid bindings\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
