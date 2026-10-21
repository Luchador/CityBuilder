#include "ui_tooltip.hpp"
#include "gpu_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
    void Fill(std::vector<UiVertex>& mesh, UiRect r, SDL_FColor c)
    {
        const UiVertex a{r.x,r.y,c.r,c.g,c.b,c.a}, b{r.x+r.w,r.y,c.r,c.g,c.b,c.a};
        const UiVertex d{r.x,r.y+r.h,c.r,c.g,c.b,c.a}, e{r.x+r.w,r.y+r.h,c.r,c.g,c.b,c.a};
        mesh.insert(mesh.end(), {a,b,e,a,e,d});
    }
}

void UiTooltip::Initialize(SDL_GPUDevice* gpu, TextRenderer& text)
{
    if (renderer || !gpu) 
    {
        throw std::logic_error("Initialize tooltip once with a valid GPU device");
    }

    device = gpu;
    renderer = &text;
    style.size = 16;
    style.color = {0.10f,0.09f,0.04f,1};
    style.verticalAlign = TextVerticalAlign::Top;
    // Each formatted run is already positioned and wrapped by LayoutTooltipText.
    style.wrap = false;
}

void UiTooltip::Hide()
{
    visible = false;
    source = nullptr;
    sourceRevision = hoverStart = 0;
    content.clear();

    for (auto* label : labels) label->SetVisible(false);
}

void UiTooltip::Release()
{
    Hide();

    for (auto* label : labels) renderer->DestroyLabel(*label);
    labels.clear();
    textLayout = {};
    renderer = nullptr;
    if (vertices) 
    {
        SDL_ReleaseGPUBuffer(device, vertices);
    }

    vertices = nullptr;
    device = nullptr;
    mesh.clear();
    measuredContent.clear();
    measuredWidth = measuredHeight = 0;
    measuredPixelWidth = measuredPixelHeight = 0;
    bounds = {};
}

void UiTooltip::Update(const UiButton* button, Uint64 now)
{
    if (!button || !button->IsVisible() || !button->IsEnabled() || !button->IsHovered() || button->HasPress() || button->GetTooltip().empty())
    {
        Hide();
        return;
    }
    if (source != button || sourceRevision != button->GetHoverRevision() || content != button->GetTooltip())
    {
        Hide();
        source = button;
        sourceRevision = button->GetHoverRevision();
        content = button->GetTooltip();
        hoverStart = now;
    }

    anchor = button->GetBounds();
    visible = now >= hoverStart && now - hoverStart >= button->GetTooltipDelayMs();
    for (auto* label : labels) label->SetVisible(visible);
}

void UiTooltip::Prepare(float width, float height, Uint32 pixelWidth, Uint32 pixelHeight)
{
    if (!visible) 
    {
        return;
    }

    if (!renderer) 
    {
        throw std::logic_error("Initialize tooltip before preparing it");
    }

    if (!std::isfinite(width) || !std::isfinite(height) || width < 32 || height < 32 || !pixelWidth || !pixelHeight)
    {
        Hide();
        return;
    }

    const float sx = static_cast<float>(pixelWidth) / width, sy = static_cast<float>(pixelHeight) / height;
    const float rasterScale = std::max(sx, sy);
    const float borderX = 1 / sx, borderY = 1 / sy; // Exactly one physical pixel.
    constexpr float margin = 4, padding = 8, separation = 8, maxTextWidth = 360;

    if (content != measuredContent || width != measuredWidth || height != measuredHeight || pixelWidth != measuredPixelWidth || pixelHeight != measuredPixelHeight)
    {
        TextStyle boldStyle = style;
        boldStyle.bold = true;
        const float lineHeight = std::max(renderer->MeasureLineHeight(style, rasterScale),
                                          renderer->MeasureLineHeight(boldStyle, rasterScale));
        const auto measure = [&](std::string_view text, bool bold)
        {
            return renderer->MeasureText(text, bold ? boldStyle : style, rasterScale).x;
        };
        const auto natural = LayoutTooltipText(content, 0, lineHeight, measure);
        const float textWidth = std::max(1.0f, std::min({std::ceil(natural.width * rasterScale) / rasterScale, maxTextWidth, width - 2 * (margin + padding + borderX)}));
        textLayout = LayoutTooltipText(content, textWidth, lineHeight, measure);
        bounds.w = std::min(std::ceil((textWidth + 2 * (padding + borderX)) * sx) / sx, std::floor((width - 2 * margin) * sx) / sx);
        bounds.h = std::min(std::ceil((textLayout.height + 2 * (padding + borderY)) * sy) / sy, std::floor((height - 2 * margin) * sy) / sy);
        // Reuse labels across tooltips instead of allocating every frame.
        labels.reserve(textLayout.runs.size());
        while (labels.size() > textLayout.runs.size())
        {
            renderer->DestroyLabel(*labels.back());
            labels.pop_back();
        }
        while (labels.size() < textLayout.runs.size())
        {
            auto& label = renderer->CreateLabel("", style);
            label.SetGroup(TextGroup);
            labels.push_back(&label);
        }
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            labels[i]->SetText(textLayout.runs[i].text);
            labels[i]->SetStyle(textLayout.runs[i].bold ? boldStyle : style);
            labels[i]->SetVisible(true);
        }
        measuredContent = content;
        measuredWidth = width; measuredHeight = height;
        measuredPixelWidth = pixelWidth; measuredPixelHeight = pixelHeight;
    }

    // Prefer a side of the button, then below/above. Keep the position stable
    // while moving within the button, and never give the tooltip a mouse hit area.
    bounds.x = anchor.x + anchor.w + separation;
    bounds.y = anchor.y;

    if (bounds.x + bounds.w > width - margin)
    {
        bounds.x = anchor.x - separation - bounds.w;
    
        if (bounds.x < margin)
        {
            bounds.x = anchor.x;
            bounds.y = anchor.y + anchor.h + separation;
        
            if (bounds.y + bounds.h > height - margin) 
            {
                bounds.y = anchor.y - separation - bounds.h;
            }
        }
    }

    bounds.x = std::floor(std::clamp(bounds.x, margin, width - margin - bounds.w) * sx) / sx;
    bounds.y = std::floor(std::clamp(bounds.y, margin, height - margin - bounds.h) * sy) / sy;

    const UiRect textArea{bounds.x + borderX + padding, bounds.y + borderY + padding,
        std::max(0.0f, bounds.w - 2 * (padding + borderX)), std::max(0.0f, bounds.h - 2 * (padding + borderY))};
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        const auto& run = textLayout.runs[i];
        labels[i]->SetBounds({textArea.x + run.bounds.x, textArea.y + run.bounds.y, run.bounds.w, run.bounds.h});
        labels[i]->SetClip(textArea);
    }

    std::vector<UiVertex> next;
    next.reserve(12);

    Fill(next, bounds, {0.16f,0.14f,0.08f,1});
    Fill(next, {bounds.x + borderX, bounds.y + borderY, bounds.w - 2 * borderX, bounds.h - 2 * borderY}, {1.0f,0.98f,0.76f,1});

    if (next.size() != mesh.size() || std::memcmp(next.data(), mesh.data(), next.size() * sizeof(UiVertex)) != 0)
    {
        auto* replacement = UploadBuffer(device, next.data(), static_cast<Uint32>(next.size() * sizeof(UiVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
    
        if (vertices) 
        {
            SDL_ReleaseGPUBuffer(device, vertices);
        }
    
        vertices = replacement;
        mesh = std::move(next);
    }
}

void UiTooltip::Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUGraphicsPipeline* pipeline, float width, float height, Uint32 pixelWidth, Uint32 pixelHeight) const
{
    if (!visible || !vertices) 
    {
        return;
    }

    const SDL_Rect clip{0,0,static_cast<int>(pixelWidth),static_cast<int>(pixelHeight)};

    SDL_SetGPUScissor(pass, &clip);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    const SDL_GPUBufferBinding binding{vertices,0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    const float screen[4]{width,height,0,0};
    SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(mesh.size()), 1, 0, 0);

    renderer->Draw(commands, pass, width, height, pixelWidth, pixelHeight, TextGroup);
}
