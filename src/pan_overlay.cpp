#include "pan_overlay.hpp"

#include <cmath>

void PanOverlay::Initialize(TextureManager& textures)
{
    circle = &textures.Load("ui/dir_circle.png");
    omni = &textures.Load("ui/omni_arrows.png");
    arrow = &textures.Load("ui/dir_arrow.png");
}

void PanOverlay::DrawImage(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image, float x, float y, float angle, float width, float height, float scale, bool groundAligned) const
{
    const UiImageTransform transform = groundAligned ? UiImageTransform{0.70710678f, -0.70710678f, 0.38511795f, 0.38511795f} : UiImageTransform{std::cos(angle), -std::sin(angle), std::sin(angle), std::cos(angle)};
    const float imageWidth = static_cast<float>(image.Width()) * scale;
    const float imageHeight = static_cast<float>(image.Height()) * scale;

    renderer.Draw(commands, pass, image, {x - imageWidth * 0.5f, y - imageHeight * 0.5f, imageWidth, imageHeight}, width, height, {}, transform);
}

void PanOverlay::Draw(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height, const AnchoredPan& pan, const PanSettings& settings) const
{
    if (!pan.Active()) 
    {
        return;
    }

    const PanMotion motion = pan.Motion(settings);

    DrawImage(renderer, commands, pass, motion.moving ? *arrow : *omni, pan.CursorX(), pan.CursorY(), motion.moving ? motion.angle : 0, width, height, 1.0f, !motion.moving);
    DrawImage(renderer, commands, pass, *circle, pan.AnchorX(), pan.AnchorY(), 0, width, height, settings.circleScale);
}

void PanOverlay::Release()
{
    circle = omni = arrow = nullptr;
}
