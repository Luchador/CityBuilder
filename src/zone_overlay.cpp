#include "zone_overlay.hpp"
#include "zone_render_data.hpp"
#include "world.hpp"

#include <stdexcept>
#include <string>

void ZoneOverlay::Initialize(SDL_GPUDevice* gpu, TextureManager& manager, const World& world)
{
    if (device || !gpu) throw std::logic_error("Invalid zone overlay initialization");
    device = gpu;
    textures = &manager;
    try
    {
        const auto count = world.GetDimensions().TileCount();
        const int last = static_cast<int>(count) - 1;
        image = manager.CreateRgba(count, count, BuildZonePixels(world, {0, 0, last, last}), {TextureContent::Data});
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = SDL_CreateGPUSampler(device, &info);
        if (!sampler) throw std::runtime_error(std::string("Create zone sampler: ") + SDL_GetError());
    }
    catch (...) { Release(); throw; }
}

void ZoneOverlay::Update(const World& world, ToolTileRect bounds)
{
    textures->UpdateRgba(*image, bounds.minX, bounds.minZ, bounds.maxX - bounds.minX + 1,
                         bounds.maxZ - bounds.minZ + 1, BuildZonePixels(world, bounds));
}

void ZoneOverlay::Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ToolPreview& preview,
                       const ZoneEvaluation& evaluation, Uint32 samplerSlot, Uint32 uniformSlot) const
{
    ZoneOverlayUniforms uniforms;
    uniforms.mapSize = static_cast<float>(image->Width());
    uniforms.opacity = ZoneOverlayOpacity;
    if (preview.tiles && preview.zoneType && evaluation.valid)
    {
        const auto r = *preview.tiles;
        const auto color = Zone::GetZoneDefinition(*preview.zoneType).color;
        uniforms.previewEnabled = 1;
        uniforms.affordable = evaluation.affordable ? 1.0f : 0.0f;
        uniforms.minX = static_cast<float>(r.minX); uniforms.minZ = static_cast<float>(r.minZ);
        uniforms.maxX = static_cast<float>(r.maxX); uniforms.maxZ = static_cast<float>(r.maxZ);
        // A white preview makes the dezone operation visible despite None's black color.
        uniforms.red = *preview.zoneType == ZoneType::None ? 0.95f : color.r;
        uniforms.green = *preview.zoneType == ZoneType::None ? 0.95f : color.g;
        uniforms.blue = *preview.zoneType == ZoneType::None ? 0.95f : color.b;
        uniforms.zoneType = static_cast<float>(*preview.zoneType);
    }
    const SDL_GPUTextureSamplerBinding binding{image->Handle(), sampler};
    SDL_BindGPUFragmentSamplers(pass, samplerSlot, &binding, 1);
    SDL_PushGPUFragmentUniformData(commands, uniformSlot, &uniforms, sizeof(uniforms));
}

void ZoneOverlay::Release() noexcept
{
    image.reset();
    if (sampler) SDL_ReleaseGPUSampler(device, sampler);
    sampler = nullptr;
    textures = nullptr;
    device = nullptr;
}
