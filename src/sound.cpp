#include "sound.hpp"

#include <limits>
#include <string>

SoundEffect::~SoundEffect()
{
    Release();
}

bool SoundEffect::Load(const char* assetPath)
{
    Release();

    const char* base = SDL_GetBasePath();
    if (!base)
    {
        SDL_Log("Cannot locate sound assets: %s", SDL_GetError());
        return false;
    }

    const std::string path = std::string(base) + "assets/" + assetPath;
    SDL_AudioSpec format{};

    if (!SDL_LoadWAV(path.c_str(), &format, &samples, &byteCount))
    {
        SDL_Log("Cannot load sound '%s': %s", path.c_str(), SDL_GetError());
        Release();
        return false;
    }

    // SDL_LoadWAV returns Uint32, but SDL_PutAudioStreamData takes int.
    if (byteCount == 0 || byteCount > static_cast<Uint32>(std::numeric_limits<int>::max()))
    {
        SDL_Log("Sound is empty or too large: %s", path.c_str());
        Release();
        return false;
    }

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr);

    if (!stream || !SDL_ResumeAudioStreamDevice(stream))
    {
        SDL_Log("Cannot open sound playback: %s", SDL_GetError());
        Release();

        return false;
    }

    return true;
}

void SoundEffect::Play()
{
    if (!stream)
    {
        return;
    }

    // Restart this effect instead of queuing repeated copies behind it.
    if (!SDL_ClearAudioStream(stream) || !SDL_PutAudioStreamData(stream, samples, static_cast<int>(byteCount)) || !SDL_FlushAudioStream(stream))
    {
        SDL_Log("Cannot play sound: %s", SDL_GetError());
    }
}

void SoundEffect::Release()
{
    if (stream)
    {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }

    SDL_free(samples);
    samples = nullptr;
    byteCount = 0;
}
