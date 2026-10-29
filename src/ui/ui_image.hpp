#pragma once

#include "ui.hpp"
#include "texture.hpp"

struct UiImageTint { float r = 1, g = 1, b = 1, a = 1; };
struct UiImageTransform { float m00 = 1, m01 = 0, m10 = 0, m11 = 1; };

// Shared premultiplied-alpha pipelines/sampler for images and masked tool buttons.
// Release before the GPU device. Textures are borrowed only for each Draw().
class UiImageRenderer
{
public:
    UiImageRenderer() = default;
    ~UiImageRenderer() { Release(); }
    UiImageRenderer(const UiImageRenderer&) = delete;
    UiImageRenderer& operator=(const UiImageRenderer&) = delete;

    void Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment,
                    SDL_GPUTextureFormat format, SDL_GPUShader* toolFragment = nullptr);
    void Release();
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image,
              UiRect bounds, float windowWidth, float windowHeight,
              UiImageTint tint = {}, UiImageTransform transform = {}) const;
    void DrawToolButton(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                        const Texture& icon, const Texture& opacity, const Texture* overlay,
                        UiRect bounds, float windowWidth, float windowHeight, UiImageTint tint = {}) const;

private:
    void DrawImage(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                   const Texture& image, const Texture* opacity, const Texture* overlay,
                   UiRect bounds, float width, float height, UiImageTint tint, UiImageTransform transform) const;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUGraphicsPipeline* toolPipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
};
