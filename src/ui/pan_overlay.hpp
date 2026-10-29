#pragma once

#include "camera.hpp"
#include "ui_image.hpp"

// Borrows cached textures; uses the same image renderer as UI buttons.
class PanOverlay
{
public:
    void Initialize(TextureManager& textures);
    void Release();
    void Draw(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height, const AnchoredPan& pan, const PanSettings& settings) const;

private:
    void DrawImage(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                   const Texture& image, float x, float y, float angle, float width, float height,
                   float scale = 1.0f, bool groundAligned = false) const;
    const Texture* circle = nullptr;
    const Texture* omni = nullptr;
    const Texture* arrow = nullptr;
};
