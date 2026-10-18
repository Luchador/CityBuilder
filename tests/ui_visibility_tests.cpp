#include "ui.hpp"
#include "ui_buttons.hpp"
#include "ui_image.hpp"
#include "ui_minimap.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

struct SDL_Window
{
    int width = 1280, height = 720;
    SDL_WindowFlags flags = SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
};

namespace
{
    float mouseX = 0, mouseY = 0;
    SDL_MouseButtonFlags mouseButtons = 0;
    int captureReleases = 0;
}

SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window* window) { return window->flags; }
bool SDLCALL SDL_GetWindowSize(SDL_Window* window, int* width, int* height)
{
    *width = window->width;
    *height = window->height;
    return true;
}
bool SDLCALL SDL_CaptureMouse(bool enabled)
{
    if (!enabled) ++captureReleases;
    return true;
}
SDL_MouseButtonFlags SDLCALL SDL_GetMouseState(float* x, float* y)
{
    *x = mouseX; *y = mouseY;
    return mouseButtons;
}

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

    constexpr std::array<UiButtonId, 6> ToolbarIds{
        UiButtonIds::TerrainButton, UiButtonIds::ZonesButton, UiButtonIds::InfrastructureButton,
        UiButtonIds::UtilitiesButton, UiButtonIds::GovernmentButton, UiButtonIds::DisastersButton
    };

    void ToolbarCompositionAndLayout()
    {
        TextureManager textures;
        UiButtons buttons;
        UiMinimap minimap;
        UiImageRenderer renderer;
        CreateUiButtons(buttons, textures);
        Check(textures.Size() == 33, "All eleven controls must load their three texture states");
        constexpr std::array<const char*, 6> stems{"terrain", "zones", "infra", "util", "gov", "disaster"};
        for (std::size_t i = 0; i < ToolbarIds.size(); ++i)
        {
            const auto* button = buttons.Find(ToolbarIds[i]);
            Check(button && button->GetTexture() == &textures.Load(std::string("ui/toolbar/btn_") + stems[i] + "_normal.png"),
                  "Toolbar ID must map to the correct asset");
            Check(button->IsEnabled() && !button->IsSelected(), "Placeholder buttons start enabled and unselected");
        }

        for (const auto [width, height] : std::array<std::array<float, 2>, 5>{{{320, 240}, {800, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}}})
        for (float mapSize : {128.0f, 288.0f, 512.0f})
        for (float barSize : {72.0f, 92.0f, 128.0f})
        {
            UiSettings settings;
            settings.minimapSize = mapSize;
            settings.barSlimDimension = barSize;
            const auto layout = CalculateUiLayout(width, height, settings);
            LayoutUiButtons(buttons, minimap, layout);
            float lastBottom = layout.toolbarContent.y;
            for (UiButtonId id : ToolbarIds)
            {
                const auto& r = buttons.Find(id)->GetBounds();
                Check(r.w > 0 && r.h > 0 && std::abs(r.w / r.h - 84.0f / 50.0f) < 0.001f,
                      "Toolbar buttons must retain the image aspect ratio");
                Check(r.x >= layout.toolbarContent.x - 0.001f &&
                      r.x + r.w <= layout.toolbarContent.x + layout.toolbarContent.w + 0.001f &&
                      r.y >= lastBottom - 0.001f &&
                      r.y + r.h <= layout.toolbarContent.y + layout.toolbarContent.h + 0.001f,
                      "Toolbar stack must fit its content area without overlapping buttons or minimap");
                Check(std::abs((r.x + r.w * 0.5f) - (layout.toolbar.x + layout.toolbar.w * 0.5f)) < 0.001f,
                      "Toolbar buttons must be horizontally centered");
                Check(!layout.IsWorldPoint(r.x + r.w * 0.5f, r.y + r.h * 0.5f), "Visible toolbar button must be outside world input");
                lastBottom = r.y + r.h;
            }
            const auto terrainBounds = buttons.Find(ToolbarIds[0])->GetBounds();
            drawn.clear();
            buttons.Draw(renderer, nullptr, nullptr, width, height);
            Check(drawn.size() == 11, "Expanded UI must render all toolbar and minimap buttons");
            SetUiMinimized(settings, buttons, true);
            LayoutUiButtons(buttons, minimap, CalculateUiLayout(width, height, settings));
            drawn.clear();
            buttons.Draw(renderer, nullptr, nullptr, width, height);
            Check(drawn.size() == 1 && !buttons.BlocksPointer(terrainBounds.x + terrainBounds.w * 0.5f,
                                                           terrainBounds.y + terrainBounds.h * 0.5f),
                  "Minimized toolbar must neither render nor block world input");
            SetUiMinimized(settings, buttons, false);
            LayoutUiButtons(buttons, minimap, layout);
            Check(SameRect(terrainBounds, buttons.Find(ToolbarIds[0])->GetBounds()), "Restore must recover toolbar placement");
        }
        LayoutUiButtons(buttons, minimap, CalculateUiLayout(1280, 720, {}));
        const auto r = buttons.Find(UiButtonIds::TerrainButton)->GetBounds();
        Check(r.w == 84 && r.h == 50 && r.y == 4, "Normal window must show native-size toolbar artwork");
    }

    void ButtonEventRouting()
    {
        TextureManager textures;
        UiButtons buttons;
        UiMinimap minimap;
        UiSettings settings;
        SDL_Window window;
        CreateUiButtons(buttons, textures);
        const auto send = [&](Uint32 type, float x, float y, Uint8 button = SDL_BUTTON_LEFT)
        {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_EVENT_MOUSE_MOTION) { event.motion.x = x; event.motion.y = y; }
            else if (type == SDL_EVENT_MOUSE_WHEEL) { event.wheel.mouse_x = x; event.wheel.mouse_y = y; }
            else { event.button.x = x; event.button.y = y; event.button.button = button; }
            return HandleUiButtonEvent(&window, event, settings, buttons, minimap);
        };

        // No layout/render has happened yet: input must calculate current bounds.
        auto down = send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29);
        Check(down.consumed && !down.clicked, "First event must hit the toolbar before the first render");
        auto* terrain = buttons.Find(UiButtonIds::TerrainButton);
        Check(terrain->GetTexture() == &textures.Load("ui/toolbar/btn_terrain_pressed.png"), "Press texture must be used");
        Check(send(SDL_EVENT_MOUSE_BUTTON_UP, 1234, 29).clicked == UiButtonIds::TerrainButton, "Placeholder click must return its ID");
        Check(terrain->GetTexture() == &textures.Load("ui/toolbar/btn_terrain_hov.png"), "Hover texture must be used");
        Check(!terrain->IsSelected(), "Placeholder click must not select a tool");
        Check(send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29, SDL_BUTTON_RIGHT).consumed,
              "Right-button press on toolbar must not start camera panning");
        Check(send(SDL_EVENT_MOUSE_WHEEL, 1234, 29).consumed, "Wheel on toolbar must not zoom the world");
        Check(!send(SDL_EVENT_MOUSE_BUTTON_UP, 1234, 29, SDL_BUTTON_RIGHT).consumed,
              "Right-button release must reach camera pan handler");

        send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29);
        Check(send(SDL_EVENT_MOUSE_MOTION, 10, 10).consumed, "Captured drag remains a UI interaction outside the button");
        const auto releasedOutside = send(SDL_EVENT_MOUSE_BUTTON_UP, 10, 10);
        Check(releasedOutside.consumed && !releasedOutside.clicked, "Releasing outside must cancel the click");

        for (Uint32 type : {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MINIMIZED, SDL_EVENT_WINDOW_HIDDEN,
                            SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED})
        {
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29);
            SDL_Event event{}; event.type = type;
            Check(!HandleUiButtonEvent(&window, event, settings, buttons, minimap).consumed && !buttons.HasCapture(),
                  "Window changes must cancel UI capture and remain visible to the camera");
            Check(!send(SDL_EVENT_MOUSE_BUTTON_UP, 1234, 29).clicked, "Canceled press must not click later");
        }

        send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29);
        SetUiMinimized(settings, buttons, true);
        Check(!buttons.HasCapture() && !send(SDL_EVENT_MOUSE_BUTTON_UP, 1234, 29).clicked,
              "Minimizing during a press must cancel it");
        Check(!send(SDL_EVENT_MOUSE_BUTTON_DOWN, 1234, 29, SDL_BUTTON_RIGHT).consumed &&
              !send(SDL_EVENT_MOUSE_WHEEL, 1234, 29).consumed,
              "Hidden toolbar must allow camera input in its former area");
        const auto restore = buttons.Find(UiButtonIds::MinimizeUIButton)->GetBounds();
        const float restoreX = restore.x + restore.w * 0.5f, restoreY = restore.y + restore.h * 0.5f;
        Check(send(SDL_EVENT_MOUSE_BUTTON_DOWN, restoreX, restoreY).consumed &&
              send(SDL_EVENT_MOUSE_BUTTON_UP, restoreX, restoreY).clicked == UiButtonIds::MinimizeUIButton,
              "Restore button must remain interactive");
        SetUiMinimized(settings, buttons, false);

        window.width = 800; window.height = 480;
        send(SDL_EVENT_MOUSE_MOTION, 0, 0);
        const auto r = terrain->GetBounds();
        mouseX = r.x + r.w * 0.5f; mouseY = r.y + r.h * 0.5f;
        Check(send(SDL_EVENT_MOUSE_BUTTON_DOWN, mouseX, mouseY).consumed, "Resize must update hit bounds before rendering");
        mouseButtons = SDL_BUTTON_LMASK;
        UpdateUiButtonPointer(&window, buttons, false);
        Check(terrain->HasPress(), "Unchanged layout/pointer update must preserve a held press");
        mouseButtons = 0;
        UpdateUiButtonPointer(&window, buttons, false);
        Check(!buttons.HasCapture() && !terrain->HasPress(), "Polling must recover from a missed release");
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, mouseX, mouseY);
        UpdateUiButtonPointer(&window, buttons, true);
        Check(!buttons.HasCapture() && !terrain->IsHovered(), "Camera pan must clear button interactions");
        window.flags = 0;
        Check(!send(SDL_EVENT_MOUSE_BUTTON_DOWN, mouseX, mouseY).consumed, "Unfocused window must ignore button input");
        Check(captureReleases > 0, "Cancel/release path must release SDL mouse capture");
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
        ToolbarCompositionAndLayout();
        ButtonEventRouting();
        std::cout << "PASS: toolbar assets/layout/states, UI visibility, input routing, resize/focus/capture recovery\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
