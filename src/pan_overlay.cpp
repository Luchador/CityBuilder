#include "pan_overlay.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    [[noreturn]] void FailOverlay(const std::string& operation)
    {
        throw std::runtime_error(operation + ": " + SDL_GetError());
    }
}

void PanOverlay::Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment,
                            SDL_GPUTextureFormat targetFormat)
{
    device = gpu;
    SDL_GPUColorTargetDescription color{};
    color.format = targetFormat;
    color.blend_state.enable_blend = true;

    // PNGs contain straight alpha. Premultiply before filtering in LoadImage.
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;

    // The shader generates six quad vertices from SV_VertexID.
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    info.target_info.num_color_targets = 1;
    info.target_info.color_target_descriptions = &color;
    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    if (!pipeline) 
    {
        FailOverlay("Create pan indicator pipeline");
    }

    SDL_GPUSamplerCreateInfo sampling{};
    sampling.min_filter = SDL_GPU_FILTER_LINEAR;
    sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampling.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampling.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &sampling);

    if (!sampler) 
    {
        FailOverlay("Create pan indicator sampler");
    }

    circle = LoadImage("dir_circle.png");
    omni = LoadImage("omni_arrows.png");
    arrow = LoadImage("dir_arrow.png");
}

PanOverlay::Image PanOverlay::LoadImage(const char* name)
{
    const char* base = SDL_GetBasePath();

    if (!base) 
    {
        FailOverlay("Find executable directory");
    }

    const std::string path = std::string(base) + "assets/ui/" + name;

    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    Surface loaded(SDL_LoadPNG(path.c_str()), SDL_DestroySurface);

    if (!loaded) 
    {
        FailOverlay("Load " + path);
    }

    Surface rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);

    if (!rgba) 
    {
        FailOverlay("Convert " + path);
    }

    // Bound sizes to avoid overflow and accidental excessive uploads.
    if (rgba->w <= 0 || rgba->h <= 0 || rgba->w > 4096 || rgba->h > 4096)
    {
        throw std::runtime_error("Pan image dimensions must be between 1 and 4096: " + path);
    }

    const Uint32 width = static_cast<Uint32>(rgba->w);
    const Uint32 height = static_cast<Uint32>(rgba->h);
    const Uint32 pitch = (width * 4 + 255u) & ~255u;

    SDL_GPUTextureCreateInfo textureInfo{};

    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);

    if (!texture) 
    {
        FailOverlay("Create texture for " + path);
    }

    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* commands = nullptr;

    try
    {
        SDL_GPUTransferBufferCreateInfo upload{};
        upload.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        upload.size = pitch * height;
        transfer = SDL_CreateGPUTransferBuffer(device, &upload);
        if (!transfer) 
        {
            FailOverlay("Create pan upload buffer");
        }
    
        auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    
        if (!mapped) 
        {
            FailOverlay("Map pan upload buffer");
        }
    
        std::memset(mapped, 0, upload.size);
    
        for (Uint32 y = 0; y < height; ++y)
        {
            const auto* source = static_cast<const Uint8*>(rgba->pixels) + y * rgba->pitch;
            Uint8* destination = mapped + y * pitch;
        
            for (Uint32 x = 0; x < width; ++x)
            {
                const Uint32 alpha = source[x * 4 + 3];
            
                for (Uint32 channel = 0; channel < 3; ++channel)
                {
                    destination[x * 4 + channel] = static_cast<Uint8>((source[x * 4 + channel] * alpha + 127) / 255);
                }
        
                destination[x * 4 + 3] = static_cast<Uint8>(alpha);
            }
        }

        SDL_UnmapGPUTransferBuffer(device, transfer);
        commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands) 
        {
            FailOverlay("Acquire pan upload commands");
        }

        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = pitch / 4;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = width;
        destination.h = height;
        destination.d = 1;
        SDL_UploadToGPUTexture(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
        commands = nullptr;

        if (!submitted) 
        {
            FailOverlay("Submit pan image upload");
        }
    
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return {texture, static_cast<float>(width), static_cast<float>(height)};
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

        SDL_ReleaseGPUTexture(device, texture);
        throw;
    }
}

/**
 * Images can optionally be ground aligned, causing them to appear paraellel to the ground from our isometric camera.
 */
void PanOverlay::DrawImage(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Image& image, float x, float y, float angle, float width, float height, float scale, bool groundAligned)
{
    const float m00 = groundAligned ? 0.70710678f : std::cos(angle);
    const float m01 = groundAligned ? -0.70710678f : -std::sin(angle);
    const float m10 = groundAligned ? 0.40824829f : std::sin(angle);
    const float m11 = groundAligned ? 0.40824829f : std::cos(angle);
    const float uniforms[12] = {width, height, 0, 0, x, y, image.width * 0.5f * scale, image.height * 0.5f * scale, m00, m01, m10, m11};

    SDL_PushGPUVertexUniformData(commands, 0, uniforms, sizeof(uniforms));
    SDL_GPUTextureSamplerBinding binding{image.texture, sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

void PanOverlay::Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height, const AnchoredPan& pan, const PanSettings& settings)
{
    if (!pan.Active()) 
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    const PanMotion motion = pan.Motion(settings);

    DrawImage(commands, pass, motion.moving ? arrow : omni, pan.CursorX(), pan.CursorY(), motion.moving ? motion.angle : 0, width, height, 1.0f, !motion.moving);

    // Keep the anchoring ring visible over the omni graphic's opaque backdrop.
    DrawImage(commands, pass, circle, pan.AnchorX(), pan.AnchorY(), 0, width, height, settings.circleScale);
}

void PanOverlay::Release()
{
    if (!device) 
    {
        return;
    }
    if (circle.texture) 
    {
        SDL_ReleaseGPUTexture(device, circle.texture);
    }

    if (omni.texture) 
    {
        SDL_ReleaseGPUTexture(device, omni.texture);
    }

    if (arrow.texture) 
    {
        SDL_ReleaseGPUTexture(device, arrow.texture);
    }

    if (sampler) 
    {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    if (pipeline) 
    {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

    circle = {}; omni = {}; arrow = {};
    sampler = nullptr;
    pipeline = nullptr;
    device = nullptr;
}
