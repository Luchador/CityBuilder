#pragma once

#include <SDL3/SDL.h>
#include "pan.hpp"

// Owns only GPU resources. Destroy before the SDL GPU device.
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

    void Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment, SDL_GPUTextureFormat targetFormat);
    void Release();
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height, const AnchoredPan& pan, const PanSettings& settings);

private:
    struct Image
    {
        SDL_GPUTexture* texture = nullptr;
        float width = 0, height = 0;
    };

    Image LoadImage(const char* name);

    void DrawImage(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Image& image, float x, float y, float angle, float width, float height, float scale = 1.0f, bool groundAligned = false);

    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    Image circle, omni, arrow;
};
