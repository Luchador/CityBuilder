#pragma once

#include "minimap_map.hpp"
#include "pan_overlay.hpp"
#include "ui_buttons.hpp"
#include "ui_minimap.hpp"
#include "ui_text.hpp"
#include "ui_window.hpp"

#include <functional>
#include <memory>

// Game actions are supplied by Application; the UI never owns the city or camera.
// Callbacks execute synchronously on the main thread and may be left empty.
struct UiActions
{
    std::function<void(float)> zoomCamera;
    std::function<void(bool)> rotateCamera;
    std::function<void(float, float)> moveCameraToMapPoint; // Normalized X/Z, 0..1.
    std::function<void()> cancelWorldInteraction;
    std::function<void(UiButtonId)> buttonClicked; // Future tools and speed controls.
    std::function<void(int)> setSimulationSpeed;
    std::function<void()> resumeSimulation;
};

// Application's single entry point for UI state, input, layout, and rendering.
// Existing components stay responsible for their own geometry and behavior.
class UiManager
{
public:
    UiManager() = default;
    ~UiManager() { Release(); }
    UiManager(const UiManager&) = delete;
    UiManager& operator=(const UiManager&) = delete;

    // The window, GPU, texture cache, and callback targets must outlive this UI.
    void Initialize(SDL_GPUDevice* gpu, SDL_Window* targetWindow, TextureManager& textures,
                    const City& city, UiActions callbacks);
    // Call after GPU idle and before destroying the GPU or shared texture cache.
    void Release();

    // Route events here before world input. A true result consumes the event.
    bool HandleEvent(const SDL_Event& event);
    void CancelInteraction();
    void SetMinimized(bool minimized);
    void ToggleMinimized();
    bool IsMinimized() const { return settings.minimized; }
    const UiSettings& GetSettings() const { return settings; }
    void SetSettings(const UiSettings& next);
    const UiLayout& GetLayout();
    bool IsWorldPoint(float x, float y);
    bool BlocksWorldKeyboard() const;
    // References remain valid until Release(); Close() hides a reusable window.
    UiWindow& AddWindow(std::string_view title, UiRect bounds);
    UiWindow* FindWindow(UiWindowId id);
    void OpenWindow(UiWindowId id);
    SDL_Rect GetWorldViewport(Uint32 pixelWidth, Uint32 pixelHeight);

    // Supply the actual simulation speed each frame: 0 = paused, 1..4 = running.
    void Update(const City& city, int simulationSpeed);
    void RebuildMinimap(const TerrainMesh& terrain, std::span<const MinimapTileOverlay> overlays = {});

    // Prepare before acquiring the frame command buffer (text/geometry uploads).
    void PrepareFrame(bool mousePanning);
    // Upload the camera outline before any render pass, using the world aspect.
    void UploadFrame(SDL_GPUCommandBuffer* commands, const Camera& camera, float mapExtent,
                     Uint32 pixelWidth, Uint32 pixelHeight);
    // Draw the UI pass after the world. Pan graphics remain visible when minimized.
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, Uint32 pixelWidth,
              Uint32 pixelHeight, const Camera& camera);

private:
    void RefreshLayout();
    void OnUiButtonClicked(UiButtonId id);
    void UpdateTicker(int simulationSpeed);
    void UpdateTickerHover();
    bool HandleMinimapEvent(const SDL_Event& event);
    SDL_GPUGraphicsPipeline* MakePipeline();
    void EnsureGeometry();
    void UpdateMinimapOutline(SDL_GPUCommandBuffer* commands, const MinimapView& view);
    bool HandleWindowEvent(const SDL_Event& event);
    UiWindow* WindowAt(float x, float y) const;
    void FocusWindow(UiWindow* target);
    void CancelWindowInteraction();
    void SyncTextInput();
    void PrepareWindows(bool mousePanning, float rasterScale);
    void DrawWindows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, Uint32 width, Uint32 height);
    void CreateNewCityWindow();

    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    TextureManager* textures = nullptr;
    UiActions actions;
    UiSettings settings;
    UiLayout layout;
    UiButtons buttons;
    UiMinimap minimap;
    TextRenderer textRenderer;
    BottomBarText bottomBarText;
    UiImageRenderer imageRenderer;
    PanOverlay panOverlay;
    std::unique_ptr<Texture> minimapTexture;
    bool initialized = false;
    bool layoutDirty = true;
    bool geometryDirty = true;
    static constexpr Uint64 PauseFlashIntervalMs = 500;
    Uint64 pauseFlashStart = 0;
    bool tickerPaused = false;
    UiWindowSkin windowSkin;
    std::vector<std::unique_ptr<UiWindow>> windows; // Back to front.
    UiWindow* focusedWindow = nullptr;
    UiWindow* capturedWindow = nullptr;
    UiWindowId nextWindowId = 1, createCityWindowId = 0;
    bool textInputActive = false;
    const void* textInputOwner = nullptr;
    struct WindowDrawRange { UiWindow* window; Uint32 first, count; };
    std::vector<WindowDrawRange> windowRanges;
    std::vector<UiVertex> windowMesh;
    SDL_GPUBuffer* windowVertices = nullptr;

    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUShader* textVertexShader = nullptr;
    SDL_GPUShader* textFragmentShader = nullptr;
    SDL_GPUShader* imageVertexShader = nullptr;
    SDL_GPUShader* imageFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vertices = nullptr;
    Uint32 vertexCount = 0;
    static constexpr Uint32 MaxOutlineVertices = 96;
    SDL_GPUBuffer* outlineVertices = nullptr;
    SDL_GPUTransferBuffer* outlineTransfer = nullptr;
    Uint32 outlineVertexCount = 0;
    std::vector<UiVertex> outlineMesh;
    MinimapView outlineView{};
    bool outlineDirty = true;
};
