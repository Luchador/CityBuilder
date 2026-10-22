#pragma once

#include "tool_feedback.hpp"

#include <array>

class SoundEffect;

// Borrows Application's loaded effects. They must outlive this player.
// Multiple tool types sharing an ID use the same SoundEffect and audio stream.
class ToolSoundPlayer
{
public:
    void Bind(ToolSuccessSound id, SoundEffect& effect);
    void SetDeniedSound(SoundEffect& effect) { deniedSound = &effect; }

    // One call per applied request, after its outcome is known. Success and
    // partial success use the tool's selected effect; rejection uses the common
    // denied effect. NoChange and an unbound/None success effect are silent.
    void Play(ToolOutcome outcome, ToolSuccessSound successSound) const;

private:
    std::array<SoundEffect*, static_cast<std::size_t>(ToolSuccessSound::Count)> sounds{};
    SoundEffect* deniedSound = nullptr;
};
