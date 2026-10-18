#include "terrain_material.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

TerrainMaterialUniforms MakeTerrainMaterialUniforms(const TerrainMaterialSettings& settings)
{
    if (!std::isfinite(settings.tilesPerRepeat) || settings.tilesPerRepeat <= 0.0f ||
        settings.tilesPerRepeat > 1024.0f || !std::isfinite(settings.textureBlend) ||
        settings.textureBlend < 0.0f || settings.textureBlend > 1.0f ||
        !std::isfinite(settings.normalStrength) || settings.normalStrength < 0.0f ||
        settings.normalStrength > 4.0f)
    {
        throw std::invalid_argument("Terrain material requires tilesPerRepeat in (0, 1024], ""textureBlend in [0, 1], and normalStrength in [0, 4]");
    }

    return {settings.tilesPerRepeat, settings.textureBlend, settings.normalStrength, settings.flipNormalY ? -1.0f : 1.0f};
}

void TerrainMaterial::Initialize(SDL_GPUDevice* gpu, TextureManager& textures,
                                 std::string_view baseColorPath, std::string_view normalPath)
{
    if (device) 
    {
        throw std::logic_error("TerrainMaterial is already initialized");
    }

    if (!gpu) 
    {
        throw std::invalid_argument("TerrainMaterial requires a GPU device");
    }

    const auto& colorImage = textures.Load(baseColorPath, {TextureContent::Color, true});
    const auto& normalImage = textures.Load(normalPath, {TextureContent::Data, true});

    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.enable_anisotropy = true;
    info.max_anisotropy = 8.0f;
    info.max_lod = static_cast<float>(std::max(colorImage.MipLevels(), normalImage.MipLevels()) - 1);
    auto* createdSampler = SDL_CreateGPUSampler(gpu, &info);

    if (!createdSampler)
    {
        throw std::runtime_error(std::string("Create terrain sampler: ") + SDL_GetError());
    }

    device = gpu;
    sampler = createdSampler;
    baseColor = &colorImage;
    normal = &normalImage;
}

void TerrainMaterial::Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const TerrainMaterialSettings& settings) const
{
    if (!device) 
    {
        throw std::logic_error("Initialize TerrainMaterial before binding it");
    }

    const auto uniforms = MakeTerrainMaterialUniforms(settings);
    const SDL_GPUTextureSamplerBinding bindings[] = {{baseColor->Handle(), sampler}, {normal->Handle(), sampler}};

    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
}

void TerrainMaterial::Release() noexcept
{
    if (sampler) 
    {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    device = nullptr;
    sampler = nullptr;
    baseColor = nullptr;
    normal = nullptr;
}
