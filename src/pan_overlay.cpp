#include "pan_overlay.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
    [[noreturn]] void FailOverlay(const std::string& operation)
    {
        throw std::runtime_error(operation + ": " + SDL_GetError());
    }
}

void PanOverlay::Initialize(SDL_GPUDevice* gpu, TextureManager& textures, SDL_GPUShader* vertex, SDL_GPUShader* fragment,
                            SDL_GPUTextureFormat targetFormat)
{
    device = gpu;
    SDL_GPUColorTargetDescription color{};
    color.format = targetFormat;
    color.blend_state.enable_blend = true;

    // TextureManager supplies pixels with premultiplied alpha.
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

    circle = &textures.Load("ui/dir_circle.png");
    omni = &textures.Load("ui/omni_arrows.png");
    arrow = &textures.Load("ui/dir_arrow.png");
}

/**
 * Images can optionally be ground aligned, causing them to appear parallel to the ground from our isometric camera.
 */
void PanOverlay::DrawImage(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image, float x, float y, float angle, float width, float height, float scale, bool groundAligned)
{
    const float m00 = groundAligned ? 0.70710678f : std::cos(angle);
    const float m01 = groundAligned ? -0.70710678f : -std::sin(angle);
    const float m10 = groundAligned ? 0.40824829f : std::sin(angle);
    const float m11 = groundAligned ? 0.40824829f : std::cos(angle);
    const float uniforms[12] = {width, height, 0, 0, x, y, static_cast<float>(image.Width()) * 0.5f * scale, static_cast<float>(image.Height()) * 0.5f * scale, m00, m01, m10, m11};

    SDL_PushGPUVertexUniformData(commands, 0, uniforms, sizeof(uniforms));
    SDL_GPUTextureSamplerBinding binding{image.Handle(), sampler};
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

    DrawImage(commands, pass, motion.moving ? *arrow : *omni, pan.CursorX(), pan.CursorY(), motion.moving ? motion.angle : 0, width, height, 1.0f, !motion.moving);

    // Keep the anchoring ring visible over the omni graphic's opaque backdrop.
    DrawImage(commands, pass, *circle, pan.AnchorX(), pan.AnchorY(), 0, width, height, settings.circleScale);
}

void PanOverlay::Release()
{
    if (!device) 
    {
        return;
    }
    if (sampler) 
    {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    if (pipeline) 
    {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

    circle = nullptr;
    omni = nullptr;
    arrow = nullptr;
    sampler = nullptr;
    pipeline = nullptr;
    device = nullptr;
}
