#include "ui_buttons.hpp"
#include "ui_image.hpp"

void UiButtons::Draw(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height) const
{
    for (const auto& entry : entries)
    {
        const auto& button = *entry.button;

        if (!button.IsVisible()) 
        {
            continue;
        }

        const UiImageTint tint = button.IsEnabled() ? UiImageTint{} : UiImageTint{0.5f, 0.5f, 0.5f, 1.0f};

        renderer.Draw(commands, pass, *button.GetTexture(), button.GetBounds(), width, height, tint);
    }
}
