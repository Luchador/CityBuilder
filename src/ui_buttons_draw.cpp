#include "ui_buttons.hpp"
#include "ui_image.hpp"

void UiButtons::Draw(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height) const
{
    for (const auto& entry : entries)
    {
        const auto& button = *entry.button;

        if (!IsShown(entry))
        {
            continue;
        }

        const UiImageTint tint = button.IsEnabled() ? UiImageTint{} : UiImageTint{0.5f, 0.5f, 0.5f, 1.0f};

        const auto& skin = button.GetSkin();
        if (skin.opacity)
        {
            const auto state = button.GetState();
            const Texture* overlay = state == UiButtonState::Pressed ? skin.pressed :
                state == UiButtonState::Highlighted ? skin.highlighted : nullptr;
            renderer.DrawToolButton(commands, pass, *button.GetTexture(), *skin.opacity,
                                    overlay, button.GetBounds(), width, height, tint);
        }
        else
        {
            renderer.Draw(commands, pass, *button.GetTexture(), button.GetBounds(), width, height, tint);
        }
    }
}
