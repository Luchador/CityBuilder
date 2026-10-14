#pragma once

#include <SDL3/SDL.h>
#include "pan.hpp"
#include "texture.hpp"

// Owns its pipeline/sampler and borrows textures from TextureManager.
// Release before the manager and the SDL GPU device.
class PanOverlay
{
public:
    ~PanOverlay() 
    { 
        Release(); 
    }

    PanOverlay() = default;
    PanOverlay(const PanOverlay&) = delete;

    PanOverlay& operator=(const PanOverlay&) = delete;

    void Initialize(SDL_GPUDevice* gpu, TextureManager& textures, SDL_GPUShader* vertex, SDL_GPUShader* fragment, SDL_GPUTextureFormat targetFormat);
    void Release();
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height, const AnchoredPan& pan, const PanSettings& settings);

private:
    void DrawImage(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image, float x, float y, float angle, float width, float height, float scale = 1.0f, bool groundAligned = false);

    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    const Texture* circle = nullptr;
    const Texture* omni = nullptr;
    const Texture* arrow = nullptr;
};
