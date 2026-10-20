#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui.hpp"

enum class TextAlign { Left, Center, Right };
enum class TextVerticalAlign { Top, Center, Bottom };

struct TextStyle
{
    // Relative to assets/. Size is in the same logical units as UiLayout.
    std::string fontAsset = "fonts/SourceSans3-Regular.ttf";
    float size = 16.0f;
    SDL_FColor color{0.94f, 0.96f, 0.95f, 1.0f};
    TextAlign align = TextAlign::Left;
    TextVerticalAlign verticalAlign = TextVerticalAlign::Center;
    bool wrap = false;
};

class TextRenderer;

// Owned by TextRenderer. References survive creation of other labels, but not
// DestroyLabel() or Release(). Use all text operations on the render thread.
class TextLabel
{
public:
    ~TextLabel();
    TextLabel(const TextLabel&) = delete;
    TextLabel& operator=(const TextLabel&) = delete;

    void SetText(std::string_view utf8);
    void SetStyle(const TextStyle& next);
    void SetBounds(UiRect next);
    void SetVisible(bool next);
    void SetGroup(std::uint32_t next);
    void SetClip(std::optional<UiRect> next);
    void SetHorizontalOffset(float next);
    SDL_FPoint Measure() const noexcept { return measured; } // After Prepare().

private:
    friend class TextRenderer;
    explicit TextLabel(bool& changed) : rendererDirty(changed) {}
    bool& rendererDirty;
    bool stringDirty = true;
    std::string value;
    TextStyle style;
    UiRect bounds;
    std::optional<UiRect> clip;
    std::uint32_t group = 0; // 0 = docks; each floating window gets its own group.
    bool visible = true;
    float horizontalOffset = 0;
    TTF_Text* text = nullptr;
    TTF_Font* font = nullptr; // Borrowed from the renderer's font cache.
    int wrapWidth = 0;
    SDL_FPoint measured{};
};

// SDL_ttf owns the glyph atlases; this class owns the label geometry and pipeline.
// Call Prepare before acquiring the frame command buffer: SDL_ttf may upload new
// glyphs. Draw then uses only prepared data, with no per-frame font rasterization.
class TextRenderer
{
public:
    TextRenderer() = default;
    ~TextRenderer() { Release(); }
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    void Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment,
                    SDL_GPUTextureFormat targetFormat);
    TextLabel& CreateLabel(std::string_view utf8, const TextStyle& style = {});
    void DestroyLabel(TextLabel& label);
    void Prepare(float pixelsPerUnit = 1.0f);
    SDL_FPoint MeasureText(std::string_view utf8, const TextStyle& style, float scale = 1.0f);
    // Width zero honors explicit line breaks only; positive widths also wrap words.
    SDL_FPoint MeasureWrappedText(std::string_view utf8, const TextStyle& style,
                                 float maxWidth, float scale = 1.0f);
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
              float logicalWidth, float logicalHeight, Uint32 pixelWidth, Uint32 pixelHeight,
              std::uint32_t group = 0) const;
    void Release() noexcept; // Destroy before the SDL_GPUDevice.

private:
    struct Batch
    {
        SDL_GPUTexture* atlas;
        Uint32 first, count;
        UiRect clip;
        std::uint32_t group;
    };
    TTF_Font* GetFont(const TextStyle& style, float scale);

    SDL_GPUDevice* device = nullptr;
    TTF_TextEngine* engine = nullptr;
    bool ttfInitialized = false;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUBuffer* vertices = nullptr;
    std::string assetRoot;
    std::map<std::pair<std::string, int>, TTF_Font*> fonts;
    std::vector<std::unique_ptr<TextLabel>> labels;
    std::vector<Batch> batches;
    float rasterScale = 0;
    bool dirty = true;
};
