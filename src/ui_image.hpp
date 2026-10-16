#pragma once

#include "ui.hpp"
#include "texture.hpp"

struct UiImageTint { float r = 1, g = 1, b = 1, a = 1; };
struct UiImageTransform { float m00 = 1, m01 = 0, m10 = 0, m11 = 1; };

// One shared premultiplied-alpha pipeline/sampler for UI images and pan sprites.
// Release before the GPU device. Textures are borrowed only for each Draw().
class UiImageRenderer
{
public:
    UiImageRenderer() = default;
    ~UiImageRenderer() { Release(); }
    UiImageRenderer(const UiImageRenderer&) = delete;
    UiImageRenderer& operator=(const UiImageRenderer&) = delete;

    void Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment, SDL_GPUTextureFormat format);
    void Release();
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image,
              UiRect bounds, float windowWidth, float windowHeight,
              UiImageTint tint = {}, UiImageTransform transform = {}) const;

private:
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
};
