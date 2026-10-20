#pragma once

#include "text.hpp"
#include "ui_button.hpp"
#include <limits>

// One reusable, non-interactive overlay. Buttons own their optional text;
// the manager supplies only the topmost button that currently owns hover.
class UiTooltip
{
public:
    static constexpr Uint64 DelayMs = 500;
    static constexpr std::uint32_t TextGroup = std::numeric_limits<std::uint32_t>::max();
    UiTooltip() = default;
    ~UiTooltip() { Release(); }
    UiTooltip(const UiTooltip&) = delete;
    UiTooltip& operator=(const UiTooltip&) = delete;

    void Initialize(SDL_GPUDevice* gpu, TextRenderer& renderer);
    void Release(); // Before the shared text renderer and GPU device.
    void Update(const UiButton* button, Uint64 now);
    void Hide();
    bool IsVisible() const { return visible; }
    const UiRect& GetBounds() const { return bounds; }
    // Measure and upload before TextRenderer::Prepare / frame command acquisition.
    void Prepare(float width, float height, Uint32 pixelWidth, Uint32 pixelHeight);
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
              SDL_GPUGraphicsPipeline* backgroundPipeline, float width, float height,
              Uint32 pixelWidth, Uint32 pixelHeight) const;

private:
    SDL_GPUDevice* device = nullptr;
    TextRenderer* renderer = nullptr;
    TextLabel* label = nullptr;
    TextStyle style;
    SDL_GPUBuffer* vertices = nullptr;
    std::vector<UiVertex> mesh;
    // Used only as an identity token, never dereferenced on subsequent frames.
    const UiButton* source = nullptr;
    std::uint64_t sourceRevision = 0;
    Uint64 hoverStart = 0;
    std::string content, measuredContent;
    UiRect anchor, bounds;
    float measuredWidth = 0, measuredHeight = 0;
    Uint32 measuredPixelWidth = 0, measuredPixelHeight = 0;
    bool visible = false;
};
