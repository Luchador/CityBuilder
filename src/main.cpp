#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "camera.hpp"
#include "sound.hpp"
#include "terrain.hpp"
#include "terrain_skirt.hpp"
#include "terrain_skirt_shaders.hpp"
#include "shaders.hpp"
#include "water.hpp"
#include "water_shaders.hpp"
#include "ui.hpp"
#include "pan_overlay.hpp"
#include "texture.hpp"
#include "text.hpp"
#include "text_shaders.hpp"
#include "ui_minimap.hpp"
#include "minimap_map.hpp"
#include "ui_text.hpp"
#include "ui_buttons.hpp"
#include "ui_image.hpp"
#include "ui_image_shaders.hpp"

[[noreturn]] static void Fail(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

struct BlobReleaser 
{
    void operator()(ID3DBlob* blob) const 
    { 
        if (blob) 
        {
            blob->Release(); 
        }
    }
};

static SDL_GPUShader* CompileShader(SDL_GPUDevice* device, const char* source, SDL_GPUShaderStage stage, Uint32 samplers = 0, const char* preamble = "", Uint32 fragmentUniforms = 0)
{
    ID3DBlob* rawCode = nullptr;
    ID3DBlob* rawErrors = nullptr;
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const std::string fullSource = std::string(preamble) + source;
    const HRESULT result = D3DCompile(fullSource.data(), fullSource.size(), vertex ? "city.vert" : "city.frag", nullptr, nullptr, "main", vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &rawCode, &rawErrors);

    std::unique_ptr<ID3DBlob, BlobReleaser> code(rawCode), errors(rawErrors);

    if (FAILED(result)) 
    {
        throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "D3DCompile failed without a diagnostic.");
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code->GetBufferPointer());
    info.code_size = code->GetBufferSize();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = stage;
    info.num_uniform_buffers = vertex ? 1u : fragmentUniforms;
    info.num_samplers = samplers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);

    if (!shader) 
    {
        Fail("Create shader");
    }

    return shader;
}

static SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* device, const void* data, Uint32 size, SDL_GPUBufferUsageFlags usage)
{
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);

    if (!buffer) 
    {
        Fail("Create mesh buffer");
    }

    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* commands = nullptr;

    try 
    {
        SDL_GPUTransferBufferCreateInfo staging{};

        staging.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        staging.size = size;
        transfer = SDL_CreateGPUTransferBuffer(device, &staging);
    
        if (!transfer) 
        {
            Fail("Create upload buffer");
        }

        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    
        if (!mapped) 
        {
            Fail("Map upload buffer");
        }

        std::memcpy(mapped, data, size);
        SDL_UnmapGPUTransferBuffer(device, transfer);

        commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands) 
        {
            Fail("Acquire upload commands");
        }

        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation source{transfer, 0};
        SDL_GPUBufferRegion destination{buffer, 0, size};
        SDL_UploadToGPUBuffer(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);

        const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
        commands = nullptr;

        if (!submitted) 
        {
            Fail("Submit mesh upload");
        }

        SDL_ReleaseGPUTransferBuffer(device, transfer);

        return buffer;
    } 
    catch (...) 
    {
        if (commands) 
        {
            SDL_CancelGPUCommandBuffer(commands);
        }
    
        if (transfer) 
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }

        SDL_ReleaseGPUBuffer(device, buffer);

        throw;
    }
}

struct Uniforms 
{
    float centerX, centerZ, halfHeight, aspect;
    float red, green, blue, depthOffset;
    CameraOrientation orientation;
};

static_assert(offsetof(Uniforms, orientation) == 32);
static_assert(sizeof(Uniforms) == 48);

class Application 
{
public:
    ~Application()
    {
        CancelButtonInteraction();
        StopMousePan();
        if (device) 
        {
            SDL_WaitForGPUIdle(device);
            textRenderer.Release();
            if (textVertexShader) SDL_ReleaseGPUShader(device, textVertexShader);
            if (textFragmentShader) SDL_ReleaseGPUShader(device, textFragmentShader);
            panOverlay.Release();
            uiButtons.Clear();
            imageRenderer.Release();
            minimapTexture.reset();
            textures.Release();
            if (imageVertexShader) SDL_ReleaseGPUShader(device, imageVertexShader);
            if (imageFragmentShader) SDL_ReleaseGPUShader(device, imageFragmentShader);
            if (terrainPipeline) SDL_ReleaseGPUGraphicsPipeline(device, terrainPipeline);
            if (skirtPipeline) SDL_ReleaseGPUGraphicsPipeline(device, skirtPipeline);
            if (skirtVertexShader) SDL_ReleaseGPUShader(device, skirtVertexShader);
            if (skirtFragmentShader) SDL_ReleaseGPUShader(device, skirtFragmentShader);
            if (skirtVertices) SDL_ReleaseGPUBuffer(device, skirtVertices);
            if (skirtIndices) SDL_ReleaseGPUBuffer(device, skirtIndices);
            if (waterPipeline) SDL_ReleaseGPUGraphicsPipeline(device, waterPipeline);
            if (waterVertexShader) SDL_ReleaseGPUShader(device, waterVertexShader);
            if (waterFragmentShader) SDL_ReleaseGPUShader(device, waterFragmentShader);
            if (waterVertices) SDL_ReleaseGPUBuffer(device, waterVertices);
            if (waterIndices) SDL_ReleaseGPUBuffer(device, waterIndices);
            if (gridPipeline) SDL_ReleaseGPUGraphicsPipeline(device, gridPipeline);
            if (uiPipeline) SDL_ReleaseGPUGraphicsPipeline(device, uiPipeline);
            if (uiVertexShader) SDL_ReleaseGPUShader(device, uiVertexShader);
            if (uiVertices) SDL_ReleaseGPUBuffer(device, uiVertices);
            if (minimapOutlineVertices) SDL_ReleaseGPUBuffer(device, minimapOutlineVertices);
            if (minimapOutlineTransfer) SDL_ReleaseGPUTransferBuffer(device, minimapOutlineTransfer);
            if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
            if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
            if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
            if (indices) SDL_ReleaseGPUBuffer(device, indices);
            if (gridVertices) SDL_ReleaseGPUBuffer(device, gridVertices);
            if (depth) SDL_ReleaseGPUTexture(device, depth);
            if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
    
        if (window)
        {
            SDL_DestroyWindow(window);
        }
    }

    void Initialize()
    {
        window = SDL_CreateWindow("Cities Unlimited", 1280, 720, SDL_WINDOW_RESIZABLE);

        if (!window) 
        {   
            Fail("Create window");
        }

        if (!SDL_SetWindowMinimumSize(window, 800, 480)) Fail("Set minimum window size");

        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");

        if (!device) 
        {
            Fail("Create Direct3D 12 device");
        }

        if (!SDL_ClaimWindowForGPUDevice(device, window)) 
        {
            Fail("Claim window");
        }

        claimed = true;
        textures.Initialize(device);

        vertexShader = CompileShader(device, VertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        fragmentShader = CompileShader(device, FragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        terrainPipeline = MakePipeline(false);
        gridPipeline = MakePipeline(true);
        skirtVertexShader = CompileShader(device, TerrainSkirtVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        skirtFragmentShader = CompileShader(device, TerrainSkirtFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, "", 1);
        skirtPipeline = MakeSkirtPipeline();
        waterVertexShader = CompileShader(device, WaterVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        waterFragmentShader = CompileShader(device, WaterFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        waterPipeline = MakeWaterPipeline();
        uiVertexShader = CompileShader(device, UiVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        uiPipeline = MakeUiPipeline();
        textVertexShader = CompileShader(device, TextVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        textFragmentShader = CompileShader(device, TextFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        textRenderer.Initialize(device, textVertexShader, textFragmentShader, SDL_GetGPUSwapchainTextureFormat(device, window));
        bottomBarText.Initialize(textRenderer);
        imageVertexShader = CompileShader(device, UiImageVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        imageFragmentShader = CompileShader(device, UiImageFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        imageRenderer.Initialize(device, imageVertexShader, imageFragmentShader, SDL_GetGPUSwapchainTextureFormat(device, window));
        panOverlay.Initialize(textures);

        // UI
        CreateUiButtons();

        // Load sound effects.
        zoomInSound.Load("sfx/ui/sfx_zoom_in.wav");
        zoomOutSound.Load("sfx/ui/sfx_zoom_out.wav");
        rotateSound.Load("sfx/ui/sfx_rotate.wav");

        // Terrain
        terrainSettings.seed = SDL_rand_bits(); // Use a fixed number here to replay a landscape.
        SDL_Log("Terrain seed: %u", static_cast<unsigned int>(terrainSettings.seed));
        terrain = GenerateTerrain(terrainSettings);
        const TerrainMesh mesh = BuildTerrain(terrain);

        RebuildMinimap(mesh);

        vertices = UploadBuffer(device, mesh.vertices.data(), static_cast<Uint32>(mesh.vertices.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        indices = UploadBuffer(device, mesh.indices.data(), static_cast<Uint32>(mesh.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
        gridVertices = UploadBuffer(device, mesh.lines.data(), static_cast<Uint32>(mesh.lines.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        indexCount = static_cast<Uint32>(mesh.indices.size());
        lineVertexCount = static_cast<Uint32>(mesh.lines.size());

        // Terrain Skirt
        const TerrainSkirtMesh skirt = BuildTerrainSkirt(terrain, skirtSettings);
        skirtUniforms = MakeTerrainSkirtUniforms(skirtSettings, terrainSettings.seed);
        skirtVertices = UploadBuffer(device, skirt.vertices.data(), static_cast<Uint32>(skirt.vertices.size() * sizeof(TerrainSkirtVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        skirtIndices = UploadBuffer(device, skirt.indices.data(), static_cast<Uint32>(skirt.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
        skirtIndexCount = static_cast<Uint32>(skirt.indices.size());

        // Water
        const WaterMesh water = BuildWater(terrain);
        waterIndexCount = static_cast<Uint32>(water.indices.size());

        if (waterIndexCount > 0)
        {
            waterVertices = UploadBuffer(device, water.vertices.data(), static_cast<Uint32>(water.vertices.size() * sizeof(WaterVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
            waterIndices = UploadBuffer(device, water.indices.data(), static_cast<Uint32>(water.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
        }
    }

    void Run()
    {
        Uint64 previous = SDL_GetTicksNS();
        bool running = true;

        while (running) 
        {
            const Uint64 now = SDL_GetTicksNS();
            const float dt = std::min(static_cast<float>((now - previous) / 1.0e9), 0.05f);

            previous = now;
            SDL_Event event;

            while (SDL_PollEvent(&event)) 
            {
                if (HandleButtonEvent(event)) 
                {
                    continue;
                }

                if (HandleMinimapEvent(event)) 
                {
                    continue;
                }

                HandlePanEvent(event);

                if (event.type == SDL_EVENT_QUIT) 
                {
                    running = false;
                }
    
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) 
                {
                    if (event.key.key == SDLK_ESCAPE) 
                    {
                        running = false;
                    }
        
                    if (event.key.key == SDLK_G) 
                    {
                        showGrid = !showGrid;
                    }

                    if (event.key.key == SDLK_TAB)
                    {
                        ToggleUi();
                    }

                    if (event.key.key == SDLK_COMMA)
                    {
                        RotateCamera(true);
                    }

                    if (event.key.key == SDLK_PERIOD)
                    {
                        RotateCamera(false);
                    }
    
                    if (event.key.key == SDLK_HOME) 
                    {
                        StopMousePan();
                        camera.SetPosition(MapExtent * 0.5f, MapExtent * 0.5f);
                        camera.ResetZoom();
                    }

                    const bool altEnter = event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT) != 0;

                    if (event.key.key == SDLK_F11 || altEnter)
                    {
                        ToggleFullscreen();
                    }
                }

                if (event.type == SDL_EVENT_MOUSE_WHEEL) 
                {
                    int windowWidth = 0, windowHeight = 0;

                    if (!SDL_GetWindowSize(window, &windowWidth, &windowHeight)) 
                    {
                        continue;
                    }

                    const auto layout = CalculateUiLayout(static_cast<float>(windowWidth), static_cast<float>(windowHeight), uiSettings);

                    if (!layout.IsWorldPoint(event.wheel.mouse_x, event.wheel.mouse_y)) 
                    {
                        continue;
                    }

                    float amount = event.wheel.y;
    
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) 
                    {
                        amount = -amount;
                    }
        
                    camera.Zoom(amount);
                }
            }

            if (!running) 
            {
                break;
            }

            if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) 
            {
                if (camera.IsMousePanning())
                {
                    float x = 0, y = 0;
                    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);

                    if (buttons & SDL_BUTTON_RMASK) 
                    {
                        camera.MoveMousePan(x, y);
                    }
                    else 
                    {
                        StopMousePan(); // Protect against a missed release event.
                    }
                }
                PanCamera(dt);
            }
            else
            {
                StopMousePan();
            }

            Render();
        }
    }

private:
    Camera camera{MapExtent};

    SoundEffect zoomInSound;
    SoundEffect zoomOutSound;
    SoundEffect rotateSound;

    static constexpr UiButtonId ZoomInButton                 = 1;
    static constexpr UiButtonId ZoomOutButton                = 2;
    static constexpr UiButtonId RotateClockwiseButton        = 3;
    static constexpr UiButtonId RotateCounterClockwiseButton = 4;
    static constexpr UiButtonId MinimizeUIButton             = 5;

    void CreateUiButtons()
    {
        auto& zoomIn = uiButtons.Add(ZoomInButton, {
            &textures.Load("ui/btn_zoom_in_normal.png"),
            &textures.Load("ui/btn_zoom_in_hov.png"),
            &textures.Load("ui/btn_zoom_in_pressed.png")
        });
        zoomIn.SetShape(UiButtonShape::Circle);

        auto& zoomOut = uiButtons.Add(ZoomOutButton, {
            &textures.Load("ui/btn_zoom_out_normal.png"),
            &textures.Load("ui/btn_zoom_out_hov.png"),
            &textures.Load("ui/btn_zoom_out_pressed.png")
        });
        zoomOut.SetShape(UiButtonShape::Circle);

        auto& rotateClockwise = uiButtons.Add(RotateClockwiseButton, {
            &textures.Load("ui/btn_rotate_cw_normal.png"),
            &textures.Load("ui/btn_rotate_cw_hov.png"),
            &textures.Load("ui/btn_rotate_cw_pressed.png")
        });
        rotateClockwise.SetShape(UiButtonShape::Rectangle);

        auto& rotateCounterClockwise =
            uiButtons.Add(RotateCounterClockwiseButton, {
                &textures.Load("ui/btn_rotate_ccw_normal.png"),
                &textures.Load("ui/btn_rotate_ccw_hov.png"),
                &textures.Load("ui/btn_rotate_ccw_pressed.png")
            });
        rotateCounterClockwise.SetShape(UiButtonShape::Rectangle);

        auto& minimizeUI =
            uiButtons.Add(MinimizeUIButton, {
                &textures.Load("ui/btn_minimize_normal.png"),
                &textures.Load("ui/btn_minimize_hov.png"),
                &textures.Load("ui/btn_minimize_pressed.png")
            });
        minimizeUI.SetShape(UiButtonShape::Rectangle);
    }

    void ToggleUi()
    {
        // A layout change invalidates mouse-pan anchors and in-progress clicks.
        StopMousePan();
        CancelButtonInteraction();

        uiSettings.minimized = !uiSettings.minimized;
        uiButtons.ShowOnly(uiSettings.minimized
            ? std::optional<UiButtonId>{MinimizeUIButton} : std::nullopt);
    }

    void ToggleFullscreen()
    {
        // Cancel interactions whose screen coordinates may change.
        StopMousePan();
        CancelButtonInteraction();

        const bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;

        if (!SDL_SetWindowFullscreen(window, !isFullscreen))
        {
            SDL_Log("Could not change fullscreen mode: %s", SDL_GetError());
        }
    }

    void LayoutUiButtons(const UiLayout& layout)
    {
        // Refresh the diamond's bounds before positioning its buttons.
        minimap.SetLayout(layout);

        const UiRect& diamond = minimap.GetDiamondBounds();

        if (diamond.w <= 0.0f || diamond.h <= 0.0f)
        {
            return;
        }

        const float scale = std::min(layout.scale, layout.minimapPanel.w / 224.0f);

        const float size = 32.0f * scale;
        const float gap = 4.0f * scale;
        const float hitInset = size / 64.0f;

        const float halfWidth = diamond.w * 0.5f;
        const float halfHeight = diamond.h * 0.5f;
        const float centerX = diamond.x + halfWidth;
        const float centerY = diamond.y + halfHeight;

        // A direction perpendicular to either lower edge, pointing outward.
        const float edgeLength = std::hypot(halfWidth, halfHeight);
        const float outwardX = halfHeight / edgeLength;
        const float outwardY = halfWidth / edgeLength;

        // Leave room for the full button rectangle, including its corners.
        const float distance = size * 0.5f * (outwardX + outwardY) + gap;

        const float offsetX = outwardX * distance;
        const float offsetY = outwardY * distance;

        // side: -1 = left, +1 = right.
        // alongEdge: 0 = side corner, 1 = bottom corner.
        const auto place = [&](UiButtonId id, float side, float alongEdge)
        {
            if (auto* button = uiButtons.Find(id))
            {
                const float x = centerX + side * (halfWidth * (1.0f - alongEdge) + offsetX);

                const float y = centerY + halfHeight * alongEdge + offsetY;

                // Convert the button's center into its upper-left corner.
                button->SetBounds({x - size * 0.5f, y - size * 0.5f, size, size});

                button->SetHitInsets({hitInset, hitInset, hitInset, hitInset});
            }
        };

        // Left side: zoom in, followed by clockwise rotation.
        place(ZoomInButton, -1.0f, 0.35f);
        place(RotateClockwiseButton, -1.0f, 0.65f);

        // Right side: zoom out, followed by counter-clockwise rotation.
        place(ZoomOutButton, 1.0f, 0.35f);
        place(RotateCounterClockwiseButton, 1.0f, 0.65f);

        // Anchor the minimize button to the minimap panel's bottom-right corner.
        if (auto* button = uiButtons.Find(MinimizeUIButton))
        {
            const UiRect& panel = layout.minimapPanel;
            const float padding = 8.0f * scale;

            button->SetBounds({panel.x + panel.w - padding - size / 1.5f, panel.y + panel.h - padding - size / 1.5f, size / 1.5f, size / 1.5f});

            // The 32x32 images have a one-pixel transparent border.
            button->SetHitInsets({scale, scale, scale, scale});
        }
    }

    void RotateCamera(bool clockwise)
    {
        // Release capture and restore the cursor before changing direction.
        StopMousePan();

        if (clockwise)
        {
            camera.RotateClockwise();
        }
        else
        {
            camera.RotateCounterClockwise();
        }

        rotateSound.Play();
    }

    void OnUiButtonClicked(UiButtonId id)
    {
        switch(id)
        {
            case ZoomInButton:
                camera.Zoom(1.0f);
                zoomInSound.Play();
                break;

            case ZoomOutButton:
                camera.Zoom(-1.0f);
                zoomOutSound.Play();
                break;

            case RotateClockwiseButton:
                RotateCamera(true);
                break;

            case RotateCounterClockwiseButton:
                RotateCamera(false);
                break;

            case MinimizeUIButton:
                ToggleUi();
                break;

            default:
                break;
        }
    }

    void CancelButtonInteraction()
    {
        if (uiButtons.HasCapture()) 
        {
            SDL_CaptureMouse(false);
        }

        uiButtons.CancelInteraction();
    }

    bool HandleButtonEvent(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
            event.type == SDL_EVENT_WINDOW_HIDDEN || event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            CancelButtonInteraction();
            return false; // Camera panning also needs to receive these events.
        }
    
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && !uiButtons.HasCapture())
        {
            uiButtons.CancelInteraction();
        }

        const bool pointerEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL;

        if (!pointerEvent || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) 
        {
            return false;
        }

        int width = 0, height = 0;

        if (SDL_GetWindowSize(window, &width, &height))
        {
            LayoutUiButtons(CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings));
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            if (event.button.button != SDL_BUTTON_LEFT)
            {
                return uiButtons.HasCapture() || uiButtons.BlocksPointer(event.button.x, event.button.y);
            }
        
            const auto result = uiButtons.PointerDown(event.button.x, event.button.y);
    
            if (result.consumed)
            {
                StopMousePan();
            }

            return result.consumed;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        {
            const bool hadCapture = uiButtons.HasCapture();
            const auto result = uiButtons.PointerUp(event.button.x, event.button.y);

            if (hadCapture) 
            {
                SDL_CaptureMouse(false);
            }
    
            if (result.clicked) 
            {
                OnUiButtonClicked(*result.clicked);
            }

            return result.consumed;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            uiButtons.PointerMove(event.motion.x, event.motion.y);
            return uiButtons.HasCapture();
        }
    
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            return uiButtons.HasCapture() || uiButtons.BlocksPointer(event.wheel.mouse_x, event.wheel.mouse_y);
        }

        return false; // In particular, let right-button releases stop panning.
    }

    void UpdateButtonPointer()
    {
        const auto flags = SDL_GetWindowFlags(window);

        if (!(flags & SDL_WINDOW_INPUT_FOCUS) || camera.IsMousePanning() || (!(flags & SDL_WINDOW_MOUSE_FOCUS) && !uiButtons.HasCapture()))
        {
            CancelButtonInteraction();
            return;
        }

        float x = 0, y = 0;
        const auto buttons = SDL_GetMouseState(&x, &y);
        const bool hadCapture = uiButtons.HasCapture();

        uiButtons.SyncPointer(x, y, (buttons & SDL_BUTTON_LMASK) != 0);

        if (hadCapture && !uiButtons.HasCapture()) 
        {
            SDL_CaptureMouse(false);
        }
    }

    bool HandleMinimapEvent(const SDL_Event& event)
    {
        if (uiSettings.minimized || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
        {
            return false;
        }

        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) 
        {
            return false;
        }

        int width = 0, height = 0;

        if (!SDL_GetWindowSize(window, &width, &height)) 
        {
            return false;
        }

        // A click can arrive before the first render, or immediately after a
        // resize. Recalculate the CPU layout instead of using stale bounds.
        // SDL mouse coordinates and these rectangles use logical window units.
        const auto layout = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);
        minimap.SetLayout(layout);

        float mapX = 0.0f, mapZ = 0.0f;

        if (minimap.TryScreenToMap(event.button.x, event.button.y, mapX, mapZ))
        {
            StopMousePan();
    
            camera.SetPosition(mapX * MapExtent, mapZ * MapExtent);

            return true;
        }

        // Reserve the visible panel for UI, including future minimap buttons.
        // The removed upper-left corner belongs to the world again.
        return layout.IsMinimapPanelPoint(event.button.x, event.button.y);
    }

    void StopMousePan()
    {
        if (!camera.IsMousePanning()) 
        {
            return;
        }

        camera.EndMousePan();

        SDL_CaptureMouse(false);

        if (restoreCursor) 
        {
            SDL_ShowCursor();
        }

        restoreCursor = false;
    }

    void HandlePanEvent(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
        {
            StopMousePan();
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                 event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                 event.type == SDL_EVENT_WINDOW_HIDDEN ||
                 event.type == SDL_EVENT_WINDOW_RESIZED ||
                 event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            // Avoid stale screen anchors after a resize or loss of focus.
            StopMousePan();
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT && !camera.IsMousePanning())
        {
            int width = 0, height = 0;
        
            if (!SDL_GetWindowSize(window, &width, &height)) 
            {
                return;
            }
    
            if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) 
            {
                return;
            }
        
            const auto layout = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);
    
            if (!layout.IsWorldPoint(event.button.x, event.button.y)) 
            {
                return;
            }
    
            camera.BeginMousePan(event.button.x, event.button.y);
            // Custom panning graphics replace the cursor in both UI modes.
            restoreCursor = SDL_CursorVisible();
    
            if (restoreCursor)
            { 
                SDL_HideCursor();
            }
    
            // Receive release/motion even outside the client area. SDL also has
            // automatic capture while buttons are held if explicit capture fails.
            if (!SDL_CaptureMouse(true)) 
            {
                SDL_Log("Mouse capture: %s", SDL_GetError());
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
           camera.MoveMousePan(event.motion.x, event.motion.y);
        }
    }

    SDL_GPUGraphicsPipeline* MakeUiPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(UiVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[2]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1].offset = offsetof(UiVertex, r);
        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = uiVertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 2;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = false;
        info.depth_stencil_state.enable_depth_write = false;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;

        // Rendered in its own pass after the terrain, with no depth attachment.
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail("Create UI pipeline");
        }

        return pipeline;
    }

    void EnsureUi()
    {
        int width = 0, height = 0;
    
        if (!SDL_GetWindowSize(window, &width, &height)) 
        {
            Fail("Get UI window size");
        }

        const auto next = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);

        if (uiVertices && next.minimized == uiLayout.minimized && next.width == uiLayout.width && next.height == uiLayout.height && next.minimapPanel.w == uiLayout.minimapPanel.w && next.toolbar.w == uiLayout.toolbar.w && next.bottomBar.h == uiLayout.bottomBar.h)
        {
            return;
        }

        minimap.SetLayout(next);
        auto mesh = BuildUiBackground(next);
        minimap.AppendGeometry(mesh);

        SDL_GPUBuffer* replacement = UploadBuffer(device, mesh.data(), static_cast<Uint32>(mesh.size() * sizeof(UiVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);

        if (uiVertices) 
        {
            SDL_ReleaseGPUBuffer(device, uiVertices);
        }
    
        uiVertices = replacement;
        uiVertexCount = static_cast<Uint32>(mesh.size());
        uiLayout = next;
        bottomBarText.SetLayout(uiLayout);
        LayoutUiButtons(uiLayout);
        minimapOutlineDirty = true;
    }

    void RebuildMinimap(const TerrainMesh& mesh, std::span<const MinimapTileOverlay> overlays = {})
    {
        // Rebuild only after world data changes, not when the camera moves
        // or the panel resizes. Future tile zone colors can be supplied here.
        const auto image = BuildMinimapImage(mesh, overlays);
        minimapTexture = textures.CreateRgba(image.width, image.height, image.rgba);
    }

    void UpdateMinimapOutline(SDL_GPUCommandBuffer* commands, const MinimapView& view)
    {
        if (!minimapOutlineDirty && view == minimapOutlineView) 
        {
            return;
        }

        minimapOutlineMesh.clear();
        minimapOutlineMesh.reserve(MaxMinimapOutlineVertices);
        minimap.AppendViewOutline(minimapOutlineMesh, view);

        if (minimapOutlineMesh.size() > MaxMinimapOutlineVertices)
        {
            throw std::runtime_error("Minimap outline exceeds its vertex buffer capacity.");
        }

        const auto count = static_cast<Uint32>(minimapOutlineMesh.size());

        if (count > 0)
        {
            // Each rectangular stroke clipped by a diamond has at most eight
            // corners (six triangles). Keep a little spare capacity and reuse
            // these buffers; cycling protects data still used by earlier frames.
            if (!minimapOutlineVertices)
            {
                SDL_GPUBufferCreateInfo info{};
                info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                info.size = MaxMinimapOutlineVertices * static_cast<Uint32>(sizeof(UiVertex));
                minimapOutlineVertices = SDL_CreateGPUBuffer(device, &info);
                if (!minimapOutlineVertices) Fail("Create minimap outline buffer");
            }

            if (!minimapOutlineTransfer)
            {
                SDL_GPUTransferBufferCreateInfo info{};
                info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                info.size = MaxMinimapOutlineVertices * static_cast<Uint32>(sizeof(UiVertex));
                minimapOutlineTransfer = SDL_CreateGPUTransferBuffer(device, &info);
                if (!minimapOutlineTransfer) Fail("Create minimap outline transfer buffer");
            }

            const Uint32 bytes = count * static_cast<Uint32>(sizeof(UiVertex));
            void* mapped = SDL_MapGPUTransferBuffer(device, minimapOutlineTransfer, true);

            if (!mapped) 
            {
                Fail("Map minimap outline transfer buffer");
            }

            std::memcpy(mapped, minimapOutlineMesh.data(), bytes);
            SDL_UnmapGPUTransferBuffer(device, minimapOutlineTransfer);

            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);

            if (!copy) 
            {
                Fail("Begin minimap outline upload");
            }

            const SDL_GPUTransferBufferLocation source{minimapOutlineTransfer, 0};
            const SDL_GPUBufferRegion destination{minimapOutlineVertices, 0, bytes};
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            SDL_EndGPUCopyPass(copy);
        }

        // Setting count to zero hides an entirely clipped outline immediately.
        minimapOutlineVertexCount = count;
        minimapOutlineView = view;
        minimapOutlineDirty = false;
    }

    SDL_GPUGraphicsPipeline* MakePipeline(bool grid)
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = sizeof(Vertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[3]{};

        // Position
        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(Vertex, x);

        // Normal
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(Vertex, nx);

        // Color
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[2].offset = offsetof(Vertex, r);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);

        // Terrain is fully opaque while the grid has partially transparent.
        color.blend_state.enable_blend = grid;

        if (grid)
        {
            // Mix the grid color with the terrain beneath it.
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;

            // Preserve the existing framebuffer alpha.
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.primitive_type = grid ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = !grid;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail(grid ? "Create grid pipeline" : "Create terrain pipeline");
        }

        return pipeline;
    }

    SDL_GPUGraphicsPipeline* MakeSkirtPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(TerrainSkirtVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[3]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(TerrainSkirtVertex, x);
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(TerrainSkirtVertex, nx);
        attributes[2].location = 2;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        attributes[2].offset = offsetof(TerrainSkirtVertex, terrainHeight);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = skirtVertexShader;
        info.fragment_shader = skirtFragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail("Create terrain skirt pipeline");
        }

        return pipeline;
    }

    SDL_GPUGraphicsPipeline* MakeWaterPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(WaterVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[2]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(WaterVertex, x);
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        attributes[1].offset = offsetof(WaterVertex, waterDepth);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = waterVertexShader;
        info.fragment_shader = waterFragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 2;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail("Create water pipeline");
        }

        return pipeline;
    }

    bool EnsureDepth(Uint32 width, Uint32 height)
    {
        if (depth && width == depthWidth && height == depthHeight) 
        {
            return true;
        }

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* replacement = SDL_CreateGPUTexture(device, &info);

        if (!replacement) 
        {
            return false;
        }

        if (depth) 
        {
            SDL_ReleaseGPUTexture(device, depth);
        }

        depth = replacement;
        depthWidth = width;
        depthHeight = height;

        return true;
    }

    void PanCamera(float dt)
    {
        int width = 0, height = 0;

        if (!SDL_GetWindowSize(window, &width, &height))
        {
            return;
        }

        const auto layout = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);

        const bool* keys = SDL_GetKeyboardState(nullptr);

        const float right = static_cast<float>(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - static_cast<float>(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);

        const float down = static_cast<float>(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) - static_cast<float>(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]);

        camera.UpdatePan(right, down, dt, layout.world.h);
    }

    void Render()
    {
        // Static UI geometry is uploaded only when the layout or visibility changes.
        // Do this before acquiring the presentation command buffer.
        EnsureUi();
        UpdateButtonPointer();

        // Font atlas uploads must be submitted before this frame's render commands.
        // Rasterize at drawable resolution, then place glyphs in logical UI units.
        int pixelWidth = 0, pixelHeight = 0;

        if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) 
        {
            Fail("Get text pixel scale");
        }

        if (!uiLayout.minimized && pixelWidth > 0 && pixelHeight > 0)
        {
            textRenderer.Prepare(std::max(static_cast<float>(pixelWidth) / uiLayout.width, static_cast<float>(pixelHeight) / uiLayout.height));
        }

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands) 
        {
            Fail("Acquire frame commands");
        }

        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0, height = 0;

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &width, &height))
        {
            const std::string error = SDL_GetError();
            SDL_CancelGPUCommandBuffer(commands);
            throw std::runtime_error("Acquire swapchain: " + error);
        }

        if (!swapchain) 
        {
            if (!SDL_SubmitGPUCommandBuffer(commands)) 
            {
                Fail("Submit minimized frame");
            }

            SDL_Delay(16);

            return;
        }

        if (!EnsureDepth(width, height)) 
        {
            const std::string error = SDL_GetError();

            // A buffer that acquired a swapchain image must be submitted, not cancelled.
            SDL_SubmitGPUCommandBuffer(commands);
    
            throw std::runtime_error("Create depth texture: " + error);
        }

        SDL_GPUColorTargetInfo color{};
        color.texture = swapchain;
        color.clear_color = {0.08f, 0.12f, 0.18f, 1.0f};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = depth;
        depthTarget.clear_depth = 1.0f;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.cycle = true;

        const int worldWidth = std::max(1, static_cast<int>(std::lround(uiLayout.world.w * static_cast<float>(width) / uiLayout.width)));
        const int worldHeight = std::max(1, static_cast<int>(std::lround(uiLayout.world.h * static_cast<float>(height) / uiLayout.height)));
    
        Uniforms view{camera.GetCenterX(), camera.GetCenterZ(), camera.GetHalfHeight(), static_cast<float>(worldWidth) / static_cast<float>(worldHeight), 1.0f, 1.0f, 1.0f, 0.0f, camera.GetOrientation()};

        // Use exactly the same camera and rounded pixel aspect as the world
        // pass. Copy the small dynamic outline before any render pass begins.
        try
        {
            if (!uiLayout.minimized)
            {
                UpdateMinimapOutline(commands, {view.centerX, view.centerZ, view.halfHeight, view.aspect, MapExtent, camera.GetQuarterTurns()});
            }
        }
        catch (...)
        {
            // Once a swapchain texture is acquired, submit rather than cancel.
            SDL_SubmitGPUCommandBuffer(commands);
            throw;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depthTarget);
        SDL_GPUViewport worldViewport{0, 0, static_cast<float>(worldWidth), static_cast<float>(worldHeight), 0, 1};
        SDL_Rect worldScissor{0, 0, worldWidth, worldHeight};
        SDL_SetGPUViewport(pass, &worldViewport);
        SDL_SetGPUScissor(pass, &worldScissor);

        // Opaque outward-facing rock walls. Drawing these before the ground
        // lets the terrain cover the shared top edge naturally.
        SDL_BindGPUGraphicsPipeline(pass, skirtPipeline);
        SDL_GPUBufferBinding skirtVertexBinding{skirtVertices, 0};
        SDL_GPUBufferBinding skirtIndexBinding{skirtIndices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &skirtVertexBinding, 1);
        SDL_BindGPUIndexBuffer(pass, &skirtIndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
        SDL_PushGPUFragmentUniformData(commands, 0, &skirtUniforms, sizeof(skirtUniforms));
        SDL_DrawGPUIndexedPrimitives(pass, skirtIndexCount, 1, 0, 0, 0);

        SDL_BindGPUGraphicsPipeline(pass, terrainPipeline);
        SDL_GPUBufferBinding vertexBinding{vertices, 0};
        SDL_GPUBufferBinding indexBinding{indices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
        SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, 0, 0, 0);

        // Render the terrain line grid.
        if (showGrid) 
        {
            SDL_BindGPUGraphicsPipeline(pass, gridPipeline);

            // Make grid partially transparent.
            constexpr float GridOpacity = 0.3f;
    
            SDL_SetGPUBlendConstants(pass, {GridOpacity, GridOpacity, GridOpacity, GridOpacity});
            SDL_GPUBufferBinding lineBinding{gridVertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &lineBinding, 1);
            view.red = view.green = view.blue = 0.0f;

            // Bring the terrain grid slightly forward to avoid z-fighting.
            view.depthOffset = -0.00001f;
            SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
            SDL_DrawGPUPrimitives(pass, lineVertexCount, 1, 0, 0);
        }

        // Draw opaque water after the grid, covering submerged lines. The grid
        // itself still follows the lake bed; there is no floating grid on water.
        if (waterIndexCount > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, waterPipeline);
            SDL_GPUBufferBinding waterVertexBinding{waterVertices, 0};
            SDL_GPUBufferBinding waterIndexBinding{waterIndices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &waterVertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &waterIndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            view.red = WaterColor.r;
            view.green = WaterColor.g;
            view.blue = WaterColor.b;
            view.depthOffset = 0.0f;
            SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
            SDL_DrawGPUIndexedPrimitives(pass, waterIndexCount, 1, 0, 0, 0);
        }

        SDL_EndGPURenderPass(pass);

        // Preserve the city image and cover the docks with opaque UI geometry.
        color.load_op = SDL_GPU_LOADOP_LOAD;
        pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        SDL_GPUViewport uiViewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        SDL_Rect uiScissor{0, 0, static_cast<int>(width), static_cast<int>(height)};
        SDL_SetGPUViewport(pass, &uiViewport);
        SDL_SetGPUScissor(pass, &uiScissor);
        if (!uiLayout.minimized)
        {
            SDL_BindGPUGraphicsPipeline(pass, uiPipeline);
            SDL_GPUBufferBinding uiBinding{uiVertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &uiBinding, 1);
            const float screen[4] = {uiLayout.width, uiLayout.height, 0, 0};
            SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
            SDL_DrawGPUPrimitives(pass, uiVertexCount, 1, 0, 0);

            if (minimapTexture)
            {
                minimap.DrawMap(imageRenderer, commands, pass, *minimapTexture, uiLayout.width, uiLayout.height);
            }

            if (minimapOutlineVertexCount > 0)
            {
                // DrawMap binds the image pipeline and its larger uniform block.
                // Restore the solid-color pipeline before drawing the red outline.
                SDL_BindGPUGraphicsPipeline(pass, uiPipeline);
                SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
                const SDL_GPUBufferBinding outlineBinding{minimapOutlineVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &outlineBinding, 1);
                SDL_DrawGPUPrimitives(pass, minimapOutlineVertexCount, 1, 0, 0);
            }

            textRenderer.Draw(commands, pass, uiLayout.width, uiLayout.height, width, height);
        }

        uiButtons.Draw(imageRenderer, commands, pass, uiLayout.width, uiLayout.height);
        panOverlay.Draw(imageRenderer, commands, pass, uiLayout.width, uiLayout.height, camera.GetMousePan(), camera.GetPanSettings());
        SDL_EndGPURenderPass(pass);

        if (!SDL_SubmitGPUCommandBuffer(commands)) 
        {
            Fail("Submit frame");
        }
    }

    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    bool claimed = false;
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* terrainPipeline = nullptr;
    SDL_GPUGraphicsPipeline* gridPipeline = nullptr;
    TerrainSkirtSettings skirtSettings;
    TerrainSkirtUniforms skirtUniforms{};
    SDL_GPUShader* skirtVertexShader = nullptr;
    SDL_GPUShader* skirtFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* skirtPipeline = nullptr;
    SDL_GPUBuffer* skirtVertices = nullptr;
    SDL_GPUBuffer* skirtIndices = nullptr;
    Uint32 skirtIndexCount = 0;
    SDL_GPUShader* waterVertexShader = nullptr;
    SDL_GPUShader* waterFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* waterPipeline = nullptr;
    SDL_GPUBuffer* waterVertices = nullptr;
    SDL_GPUBuffer* waterIndices = nullptr;
    Uint32 waterIndexCount = 0;
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUBuffer* indices = nullptr;
    SDL_GPUBuffer* gridVertices = nullptr;
    SDL_GPUTexture* depth = nullptr;
    TerrainSettings terrainSettings;
    TerrainHeightmap terrain;
    TextureManager textures;
    UiImageRenderer imageRenderer;
    UiButtons uiButtons;
    TextRenderer textRenderer;
    BottomBarText bottomBarText;
    SDL_GPUShader* textVertexShader = nullptr;
    SDL_GPUShader* textFragmentShader = nullptr;
    PanOverlay panOverlay;
    bool restoreCursor = false;
    SDL_GPUShader* imageVertexShader = nullptr;
    SDL_GPUShader* imageFragmentShader = nullptr;
    UiSettings uiSettings;
    UiLayout uiLayout;
    UiMinimap minimap;
    std::unique_ptr<Texture> minimapTexture;
    SDL_GPUShader* uiVertexShader = nullptr;
    SDL_GPUGraphicsPipeline* uiPipeline = nullptr;
    SDL_GPUBuffer* uiVertices = nullptr;
    Uint32 uiVertexCount = 0;
    static constexpr Uint32 MaxMinimapOutlineVertices = 96;
    SDL_GPUBuffer* minimapOutlineVertices = nullptr;
    SDL_GPUTransferBuffer* minimapOutlineTransfer = nullptr;
    Uint32 minimapOutlineVertexCount = 0;
    std::vector<UiVertex> minimapOutlineMesh;
    MinimapView minimapOutlineView{};
    bool minimapOutlineDirty = true;
    Uint32 depthWidth = 0, depthHeight = 0;
    Uint32 indexCount = 0, lineVertexCount = 0;
    bool showGrid = true;
};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    int exitCode = 0;

    try 
    {
        Application app;
        app.Initialize();
        app.Run();
    } 
    catch (const std::exception& error) 
    {
        SDL_Log("City Builder: %s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "City Builder", error.what(), nullptr);
        exitCode = 1;
    }

    SDL_Quit();

    return exitCode;
}
