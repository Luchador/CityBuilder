#pragma once

#include <SDL3/SDL.h>

class SoundEffect
{
public:
    SoundEffect() = default;
    ~SoundEffect();

    SoundEffect(const SoundEffect&) = delete;
    SoundEffect& operator=(const SoundEffect&) = delete;

    // Path relative to assets/, such as "sfx/ui/sfx_zoom_in.wav".
    bool Load(const char* assetPath);
    void Play();
    void Release();

private:
    SDL_AudioStream* stream = nullptr;
    Uint8* samples = nullptr;
    Uint32 byteCount = 0;
};