#include "tool_sound_player.hpp"
#include "sound.hpp"

#include <stdexcept>

void ToolSoundPlayer::Bind(ToolSuccessSound id, SoundEffect& effect)
{
    const auto index = static_cast<std::size_t>(id);
    if (id == ToolSuccessSound::None || index >= sounds.size())
    {
        throw std::invalid_argument("Bind a named tool success sound, not None or Count");
    }
    sounds[index] = &effect;
}

void ToolSoundPlayer::Play(ToolOutcome outcome, ToolSuccessSound successSound) const
{
    SoundEffect* effect = nullptr;
    switch (outcome)
    {
        case ToolOutcome::Success:
        case ToolOutcome::PartialSuccess:
        {
            const auto index = static_cast<std::size_t>(successSound);
            if (successSound != ToolSuccessSound::None && index < sounds.size())
            {
                effect = sounds[index];
            }
            break;
        }
        case ToolOutcome::Rejected:
            effect = deniedSound;
            break;
        case ToolOutcome::NoChange:
            break;
    }
    if (effect)
    {
        effect->Play();
    }
}
