#include "ui.hpp"
#include "ui_buttons.hpp"
#include "ui_image.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

// Minimal image/texture doubles: exercise the real button draw filtering without
// starting SDL or requiring Direct3D. Images have identity but no GPU allocation.
Texture::~Texture() = default;
void TextureManager::Release() noexcept { textures.clear(); }
const Texture& TextureManager::Load(std::string_view name)
{
    auto& image = textures[std::string(name)];
    if (!image) image.reset(new Texture(nullptr));
    return *image;
}
void UiImageRenderer::Release() {}

namespace
{
    std::vector<const Texture*> drawn;

    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool SameRect(const UiRect& a, const UiRect& b)
    {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }

    void ViewportAndPanelInput()
    {
        for (const auto [width, height] : std::array<std::array<float, 2>, 4>{{{800, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}}})
        {
            for (const float mapSize : {128.0f, 288.0f, 512.0f})
            {
                UiSettings settings;
                settings.minimapSize = mapSize;
                const auto shown = CalculateUiLayout(width, height, settings);
                settings.minimized = true;
                const auto hidden = CalculateUiLayout(width, height, settings);
                Check(hidden.minimized && !shown.minimized, "Visibility state did not reach layout");
                Check(SameRect(hidden.world, {0, 0, width, height}), "Hidden UI did not expand the world viewport");
                Check(SameRect(shown.minimapPanel, hidden.minimapPanel), "Restore button anchor would move");
                Check(SameRect(shown.minimapContent, hidden.minimapContent), "Hidden panel dimensions changed");
                const float mapX = shown.minimapPanel.x + shown.minimapPanel.w * 0.5f;
                const float mapY = shown.minimapPanel.y + shown.minimapPanel.h * 0.5f;
                Check(shown.IsMinimapPanelPoint(mapX, mapY), "Visible minimap lost its input area");
                Check(!hidden.IsMinimapPanelPoint(mapX, mapY) && hidden.IsWorldPoint(mapX, mapY), "Invisible minimap blocks world input");
                Check(hidden.IsWorldPoint(20, height - 10) && !shown.IsWorldPoint(20, height - 10), "Invisible bottom bar blocks world input");
                Check(hidden.IsWorldPoint(width - 10, 20) && !shown.IsWorldPoint(width - 10, 20), "Invisible toolbar blocks world input");
                Check(!hidden.IsWorldPoint(-1, 0) && !hidden.IsWorldPoint(width, height), "World hit testing escaped window");
                settings.minimized = false;
                Check(SameRect(CalculateUiLayout(width, height, settings).world, shown.world), "Restoring UI changed the original viewport");
            }
        }
    }

    void ButtonDrawingAndInput()
    {
        TextureManager textures;
        const auto& normal = textures.Load("normal");
        const auto& highlighted = textures.Load("highlighted");
        const auto& pressed = textures.Load("pressed");
        const UiButtonTextures images{&normal, &highlighted, &pressed};
        UiButtons buttons;
        auto& camera = buttons.Add(1, images);
        auto& restore = buttons.Add(5, images);
        camera.SetBounds({10, 10, 30, 30});
        restore.SetBounds({100, 100, 22, 22});
        UiImageRenderer renderer;
        const auto draw = [&]
        {
            drawn.clear();
            buttons.Draw(renderer, nullptr, nullptr, 1280, 720);
        };
        draw();
        Check(drawn.size() == 2, "Normal UI did not draw both buttons");
        Check(buttons.PointerDown(20, 20).consumed && buttons.HasCapture(), "Camera button did not capture press");
        buttons.ShowOnly(5);
        Check(!buttons.HasCapture() && !camera.HasPress(), "Minimizing left a press captured");
        Check(!buttons.PointerUp(20, 20).clicked && !buttons.BlocksPointer(20, 20), "Invisible button received a click");
        draw();
        Check(drawn.size() == 1, "Minimized UI drew extra buttons");
        buttons.PointerMove(110, 110);
        Check(restore.GetState() == UiButtonState::Highlighted, "Restore button did not hover");
        Check(buttons.PointerDown(110, 110).consumed, "Restore button cannot be pressed");
        draw();
        Check(drawn.size() == 1 && drawn[0] == &pressed, "Restore pressed texture is not drawn");
        Check(buttons.PointerUp(110, 110).clicked == 5, "Restore button cannot be clicked");

        // Newly added controls are also filtered until the UI is restored.
        auto& later = buttons.Add(6, images);
        later.SetBounds({200, 100, 22, 22});
        Check(!buttons.BlocksPointer(210, 110), "New button escaped minimized visibility");
        camera.SetSelected(true);
        camera.SetEnabled(false);
        later.SetVisible(false);
        buttons.ShowOnly(std::nullopt);
        Check(camera.IsSelected() && !camera.IsEnabled() && !later.IsVisible(), "Restoring UI overwrote individual button state");
        Check(buttons.BlocksPointer(20, 20) && !buttons.BlocksPointer(210, 110), "Restored hit testing is incorrect");
        draw();
        Check(drawn.size() == 2, "Restored draw filter is incorrect");
        camera.SetEnabled(true);
        Check(buttons.PointerDown(20, 20).consumed && buttons.PointerUp(20, 20).clicked == 1, "Restored camera button cannot be clicked");

        buttons.ShowOnly(5);
        buttons.PointerDown(110, 110);
        buttons.ShowOnly(std::nullopt); // Tab while the mouse button is held.
        Check(!buttons.HasCapture() && !buttons.PointerUp(110, 110).clicked, "Visibility change caused a delayed click");
    }
}

void UiImageRenderer::Draw(SDL_GPUCommandBuffer*, SDL_GPURenderPass*, const Texture& image,
                           UiRect, float, float, UiImageTint, UiImageTransform) const
{
    drawn.push_back(&image);
}

int main()
{
    try
    {
        ViewportAndPanelInput();
        ButtonDrawingAndInput();
        std::cout << "PASS: full-window viewport, hidden panel input, button draw/hit filtering, restored state, press cancellation\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
