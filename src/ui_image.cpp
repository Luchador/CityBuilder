#include "ui_image.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
    [[noreturn]] void FailImage(const char* operation)
    {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}

void UiImageRenderer::Initialize(SDL_GPUDevice* gpu, SDL_GPUShader* vertex, SDL_GPUShader* fragment, SDL_GPUTextureFormat format)
{
    if (device) throw std::logic_error("UI image renderer is already initialized.");
    if (!gpu || !vertex || !fragment) throw std::invalid_argument("UI images require a device and shaders.");
    device = gpu;
    try
    {
        SDL_GPUColorTargetDescription color{};
        color.format = format;
        color.blend_state.enable_blend = true;
        color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex;
        info.fragment_shader = fragment;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!pipeline) FailImage("Create UI image pipeline");
        SDL_GPUSamplerCreateInfo sampling{};
        sampling.min_filter = SDL_GPU_FILTER_LINEAR;
        sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampling.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampling.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = SDL_CreateGPUSampler(device, &sampling);
        if (!sampler) FailImage("Create UI image sampler");
    }
    catch (...)
    {
        Release();
        throw;
    }
}

void UiImageRenderer::Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Texture& image,
                           UiRect bounds, float width, float height, UiImageTint tint, UiImageTransform transform) const
{
    if (!pipeline) throw std::logic_error("Initialize UI image renderer before drawing.");
    if (width <= 0 || height <= 0 || bounds.w <= 0 || bounds.h <= 0 || tint.a <= 0) return;
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(bounds.x) ||
        !std::isfinite(bounds.y) || !std::isfinite(bounds.w) || !std::isfinite(bounds.h)) return;
    const float uniforms[16] = {
        width, height, 0, 0,
        bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f, bounds.w * 0.5f, bounds.h * 0.5f,
        transform.m00, transform.m01, transform.m10, transform.m11,
        tint.r, tint.g, tint.b, tint.a
    };
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(commands, 0, uniforms, sizeof(uniforms));
    const SDL_GPUTextureSamplerBinding binding{image.Handle(), sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

void UiImageRenderer::Release()
{
    if (!device) return;
    if (sampler) SDL_ReleaseGPUSampler(device, sampler);
    if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    sampler = nullptr;
    pipeline = nullptr;
    device = nullptr;
}
