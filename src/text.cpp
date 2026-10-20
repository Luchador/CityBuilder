#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
    struct TextVertex
    {
        float x, y, u, v;
        SDL_FColor color;
    };
    static_assert(sizeof(TextVertex) == 32);

    [[noreturn]] void FailText(const std::string& operation)
    {
        throw std::runtime_error(operation + ": " + SDL_GetError());
    }

    bool SameColor(SDL_FColor a, SDL_FColor b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    SDL_GPUBuffer* UploadVertices(SDL_GPUDevice* device, const std::vector<TextVertex>& data)
    {
        if (data.empty()) return nullptr;
        if (data.size() > std::numeric_limits<Uint32>::max() / sizeof(TextVertex))
            throw std::length_error("Text geometry exceeds the GPU buffer size limit");
        const auto size = static_cast<Uint32>(data.size() * sizeof(TextVertex));
        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = size;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &bufferInfo);
        if (!buffer) FailText("Create text geometry buffer");
        SDL_GPUTransferBuffer* transfer = nullptr;
        SDL_GPUCommandBuffer* commands = nullptr;
        try
        {
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = size;
            transfer = SDL_CreateGPUTransferBuffer(device, &info);
            if (!transfer) FailText("Create text transfer buffer");
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (!mapped) FailText("Map text transfer buffer");
            std::memcpy(mapped, data.data(), size);
            SDL_UnmapGPUTransferBuffer(device, transfer);
            commands = SDL_AcquireGPUCommandBuffer(device);
            if (!commands) FailText("Acquire text upload commands");
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy) FailText("Begin text upload pass");
            SDL_GPUTransferBufferLocation source{transfer, 0};
            SDL_GPUBufferRegion destination{buffer, 0, size};
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
            commands = nullptr;
            if (!submitted) FailText("Submit text geometry");
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return buffer;
        }
        catch (...)
        {
            if (commands) SDL_CancelGPUCommandBuffer(commands);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
            SDL_ReleaseGPUBuffer(device, buffer);
            throw;
        }
    }
}

TextLabel::~TextLabel()
{
    if (text) 
    {
        TTF_DestroyText(text);
    }
}

void TextLabel::SetText(std::string_view utf8)
{
    if (value == utf8) 
    {
        return;
    }

    value = utf8;

    stringDirty = rendererDirty = true;
}

void TextLabel::SetStyle(const TextStyle& next)
{
    if (!std::isfinite(next.size) || next.size <= 0 || next.fontAsset.empty())
    {
        throw std::invalid_argument("Text needs a font asset and a positive finite size");
    }

    if (style.fontAsset == next.fontAsset && style.size == next.size && SameColor(style.color, next.color) && style.align == next.align && style.verticalAlign == next.verticalAlign && style.wrap == next.wrap) 
    {
        return;
    }

    style = next;
    rendererDirty = true;
}

void TextLabel::SetBounds(UiRect next)
{
    if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.w) || !std::isfinite(next.h) || next.w < 0 || next.h < 0)
    {
        throw std::invalid_argument("Text bounds must be finite and have nonnegative size");
    }

    if (bounds.x == next.x && bounds.y == next.y && bounds.w == next.w && bounds.h == next.h) 
    {
        return;
    }

    bounds = next;
    rendererDirty = true;
}

void TextLabel::SetVisible(bool next)
{
    if (visible != next) 
    { 
        visible = next; rendererDirty = true; 
    }
}

void TextLabel::SetGroup(std::uint32_t next)
{
    if (group != next) 
    { 
        group = next; rendererDirty = true; 
    }
}

void TextLabel::SetClip(std::optional<UiRect> next)
{
    if (!clip && !next) 
    {
        return;
    }

    if (clip && next && clip->x == next->x && clip->y == next->y && clip->w == next->w && clip->h == next->h) 
    {
        return;
    }

    clip = next;
    rendererDirty = true;
}

void TextLabel::SetHorizontalOffset(float next)
{
    if (horizontalOffset != next) 
    { 
        horizontalOffset = next; 
        rendererDirty = true; 
    }
}

void TextRenderer::Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment, SDL_GPUTextureFormat targetFormat)
{
    if (device || !gpu) 
    {
        throw std::logic_error("Initialize text renderer once with a valid GPU device");
    }

    device = gpu;

    if (!TTF_Init()) 
    {
        FailText("Initialize SDL_ttf");
    }

    ttfInitialized = true;
    const char* base = SDL_GetBasePath();

    if (!base) 
    {
        FailText("Get font asset directory");
    }

    assetRoot = std::string(base) + "assets/";
    engine = TTF_CreateGPUTextEngine(device);

    if (!engine) 
    {
        FailText("Create GPU text engine");
    }

    SDL_GPUVertexBufferDescription buffer{};
    buffer.pitch = sizeof(TextVertex);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(TextVertex, u);
    attributes[2].location = 2;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[2].offset = offsetof(TextVertex, color);
    SDL_GPUColorTargetDescription color{};
    color.format = targetFormat;
    color.blend_state.enable_blend = true;
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    if (!pipeline) 
    {
        FailText("Create text pipeline");
    }

    SDL_GPUSamplerCreateInfo sampling{};
    sampling.min_filter = sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampling.address_mode_u = sampling.address_mode_v = sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &sampling);

    if (!sampler) 
    {
        FailText("Create text sampler");
    }
}

TextLabel& TextRenderer::CreateLabel(std::string_view utf8, const TextStyle& style)
{
    if (!engine) 
    {
        throw std::logic_error("Initialize the text renderer before creating labels");
    }

    auto label = std::unique_ptr<TextLabel>(new TextLabel(dirty));
    label->SetStyle(style);
    label->SetText(utf8);
    labels.push_back(std::move(label));
    dirty = true;

    return *labels.back();
}

void TextRenderer::DestroyLabel(TextLabel& label)
{
    const auto found = std::find_if(labels.begin(), labels.end(), [&label](const auto& item) { return item.get() == &label; });
    if (found == labels.end()) 
    {
        throw std::invalid_argument("Label belongs to a different text renderer");
    }

    labels.erase(found);
    dirty = true;
}

TTF_Font* TextRenderer::GetFont(const TextStyle& style, float scale)
{
    const float pixels = std::round(style.size * scale);
    if (pixels < 1 || pixels > 256)
    {
        throw std::invalid_argument("Font raster size must be between 1 and 256 pixels");
    }

    const auto key = std::make_pair(style.fontAsset, static_cast<int>(pixels));

    if (const auto found = fonts.find(key); found != fonts.end()) 
    {
        return found->second;
    }

    const std::string path = assetRoot + style.fontAsset;
    TTF_Font* font = TTF_OpenFont(path.c_str(), pixels);
    if (!font) 
    {
        FailText("Open font " + path);
    }

    try 
    { 
        fonts.emplace(key, font); 
    }
    catch (...) 
    { 
        TTF_CloseFont(font); 
        throw; 
    }

    return font;
}

void TextRenderer::Prepare(float pixelsPerUnit)
{
    if (!engine) 
    {
        throw std::logic_error("Initialize text renderer before preparing text");
    }

    if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0)
    {
        throw std::invalid_argument("Text pixel scale must be positive and finite");
    }

    if (rasterScale != pixelsPerUnit) 
    {
        dirty = true;
    }

    if (!dirty) 
    {
        return;
    }

    std::vector<TextVertex> mesh;
    std::vector<Batch> nextBatches;

    for (auto& item : labels)
    {
        auto& label = *item;
    
        if (!label.visible) 
        {
            continue;
        }

        TTF_Font* font = GetFont(label.style, pixelsPerUnit);
    
        if (!label.text)
        {
            label.text = TTF_CreateText(engine, font, label.value.c_str(), label.value.size());

            if (!label.text) 
            {
                FailText("Create text label");
            }

            label.font = font;
            label.stringDirty = false;
        }

        if (label.font != font)
        {
            if (!TTF_SetTextFont(label.text, font)) 
            {
                FailText("Set label font");
            }

            label.font = font;
        }

        if (label.stringDirty)
        {
            if (!TTF_SetTextString(label.text, label.value.c_str(), label.value.size())) 
            {
                FailText("Set label text");
            }

            label.stringDirty = false;
        }

        const int wrapWidth = label.style.wrap ? std::max(1, static_cast<int>(label.bounds.w * pixelsPerUnit)) : 0;

        if (label.wrapWidth != wrapWidth)
        {
            if (!TTF_SetTextWrapWidth(label.text, wrapWidth)) 
            {
                FailText("Set text wrapping");
            }

            label.wrapWidth = wrapWidth;
        }

        if (!TTF_UpdateText(label.text)) 
        {
            FailText("Prepare text glyphs");
        }

        int width = 0, height = 0;

        if (!TTF_GetTextSize(label.text, &width, &height)) 
        {
            FailText("Measure text");
        }

        label.measured = {width / pixelsPerUnit, height / pixelsPerUnit};
        if (label.bounds.w <= 0 || label.bounds.h <= 0) 
        {
            continue;
        }

        float x = label.bounds.x + label.horizontalOffset, y = label.bounds.y;
        const float extraX = label.bounds.w - label.measured.x;
        const float extraY = std::max(0.0f, label.bounds.h - label.measured.y);

        if (label.style.align == TextAlign::Center) 
        {
            x += extraX * 0.5f;
        }

        if (label.style.align == TextAlign::Right) 
        {
            x += extraX;
        }

        if (label.style.verticalAlign == TextVerticalAlign::Center) 
        {
            y += extraY * 0.5f;
        }
    
        if (label.style.verticalAlign == TextVerticalAlign::Bottom) 
        {
            y += extraY;
        }

        // Align origins to physical pixels for crisp small UI lettering.
        x = std::round(x * pixelsPerUnit) / pixelsPerUnit;
        y = std::round(y * pixelsPerUnit) / pixelsPerUnit;

        for (auto* seq = TTF_GetGPUTextDrawData(label.text); seq; seq = seq->next)
        {
            // Plain grayscale and bitmap-color fonts use the same shader.
            // SDF and underline/strike-through fill geometry need other shaders.
            if (!seq->atlas_texture || (seq->image_type != TTF_IMAGE_ALPHA && seq->image_type != TTF_IMAGE_COLOR))
            {
                throw std::runtime_error("This text renderer supports normal glyph images, not SDF or solid text decorations");
            }

            const auto first = static_cast<Uint32>(mesh.size());

            for (int i = 0; i < seq->num_indices; ++i)
            {
                const int index = seq->indices[i];
    
                // SDL_ttf's GPU positions are Y-up; our UI coordinates are Y-down.
                mesh.push_back({x + seq->xy[index].x / pixelsPerUnit, y - seq->xy[index].y / pixelsPerUnit, seq->uv[index].x, seq->uv[index].y, label.style.color});
            }

            UiRect clip = label.bounds;

            if (label.clip)
            {
                const float right = std::min(clip.x + clip.w, label.clip->x + label.clip->w);
                const float bottom = std::min(clip.y + clip.h, label.clip->y + label.clip->h);
                clip.x = std::max(clip.x, label.clip->x);
                clip.y = std::max(clip.y, label.clip->y);
                clip.w = std::max(0.0f, right - clip.x);
                clip.h = std::max(0.0f, bottom - clip.y);
            }

            nextBatches.push_back({seq->atlas_texture, first, static_cast<Uint32>(seq->num_indices), clip, label.group});
        }
    }
    SDL_GPUBuffer* replacement = UploadVertices(device, mesh);

    if (vertices) 
    {
        SDL_ReleaseGPUBuffer(device, vertices);
    }

    vertices = replacement;
    batches = std::move(nextBatches);
    rasterScale = pixelsPerUnit;
    dirty = false;
}

SDL_FPoint TextRenderer::MeasureText(std::string_view utf8, const TextStyle& style, float scale)
{
    if (utf8.empty()) 
    {
        return {};
    }

    int width = 0, height = 0;

    if (!TTF_GetStringSize(GetFont(style, scale), utf8.data(), utf8.size(), &width, &height))
    {
        FailText("Measure input text");
    }

    return {width / scale, height / scale};
}

SDL_FPoint TextRenderer::MeasureWrappedText(std::string_view utf8, const TextStyle& style, float maxWidth, float scale)
{
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(maxWidth) || maxWidth < 0 ||
        static_cast<double>(maxWidth) * scale > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid wrapped text measurement size");
    if (utf8.empty()) return {};
    const int wrap = maxWidth > 0 ? std::max(1, static_cast<int>(maxWidth * scale)) : 0;
    int width = 0, height = 0;
    if (!TTF_GetStringSizeWrapped(GetFont(style, scale), utf8.data(), utf8.size(), wrap, &width, &height))
        FailText("Measure wrapped text");
    return {width / scale, height / scale};
}

void TextRenderer::Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float logicalWidth, float logicalHeight, Uint32 pixelWidth, Uint32 pixelHeight, std::uint32_t group) const
{
    if (!vertices || batches.empty() || dirty) 
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUBufferBinding binding{vertices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    const float screen[4] = {logicalWidth, logicalHeight, 0, 0};
    SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
    const float sx = static_cast<float>(pixelWidth) / logicalWidth;
    const float sy = static_cast<float>(pixelHeight) / logicalHeight;

    for (const auto& batch : batches)
    {
        if (batch.group != group) 
        {
            continue;
        }

        const int left = static_cast<int>(std::clamp(std::ceil(batch.clip.x * sx), 0.0f, static_cast<float>(pixelWidth)));
        const int top = static_cast<int>(std::clamp(std::ceil(batch.clip.y * sy), 0.0f, static_cast<float>(pixelHeight)));
        const int right = static_cast<int>(std::clamp(std::floor((batch.clip.x + batch.clip.w) * sx), 0.0f, static_cast<float>(pixelWidth)));
        const int bottom = static_cast<int>(std::clamp(std::floor((batch.clip.y + batch.clip.h) * sy), 0.0f, static_cast<float>(pixelHeight)));

        if (right <= left || bottom <= top) 
        {
            continue;
        }

        SDL_Rect scissor{left, top, right - left, bottom - top};
        SDL_SetGPUScissor(pass, &scissor);
        SDL_GPUTextureSamplerBinding image{batch.atlas, sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &image, 1);
        SDL_DrawGPUPrimitives(pass, batch.count, 1, batch.first, 0);
    }

    SDL_Rect full{0, 0, static_cast<int>(pixelWidth), static_cast<int>(pixelHeight)};
    SDL_SetGPUScissor(pass, &full); // The pan overlay is drawn after the text.
}

void TextRenderer::Release() noexcept
{
    labels.clear();
    batches.clear();

    if (vertices) 
    {
        SDL_ReleaseGPUBuffer(device, vertices);
    }

    if (sampler) 
    {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    if (pipeline) 
    {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

    if (engine) 
    {
        TTF_DestroyGPUTextEngine(engine);
    }

    for (const auto& [key, font] : fonts) 
    {
        TTF_CloseFont(font);
    }

    fonts.clear();

    if (ttfInitialized) 
    {
        TTF_Quit();
    }

    vertices = nullptr;
    sampler = nullptr;
    pipeline = nullptr;
    engine = nullptr;
    device = nullptr;
    ttfInitialized = false;
    rasterScale = 0;
    dirty = true;
}
