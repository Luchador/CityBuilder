#include "ui_minimap.hpp"
#include "ui_image.hpp"

void UiMinimap::DrawMap(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image, float windowWidth, float windowHeight) const
{
    if (diamondBounds.w <= 0 || diamondBounds.h <= 0)
    {
        return;
    }

    // Texture U is map X, V is map Z. Transform the square into the diamond:
    // (0,0) -> top, (1,0) -> right, (1,1) -> bottom, (0,1) -> left.
    // This is MapToScreen expressed as a centered 2x2 image transform.
    const UiImageTransform transform{
        0.5f, -0.5f * diamondBounds.w / diamondBounds.h,
        0.5f * diamondBounds.h / diamondBounds.w, 0.5f
    };

    renderer.Draw(commands, pass, image, diamondBounds, windowWidth, windowHeight, {}, transform);
}
