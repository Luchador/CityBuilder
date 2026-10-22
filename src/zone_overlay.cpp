#include "zone_overlay.hpp"
#include "zone_render_data.hpp"
#include "tool.hpp"
#include "world.hpp"

#include <stdexcept>
#include <string>
#include <algorithm>

void ZoneOverlay::Initialize(SDL_GPUDevice* gpu, TextureManager& manager, const World& world)
{
    if (device || !gpu) 
    {
        throw std::logic_error("Invalid zone overlay initialization");
    }

    device = gpu;
    textures = &manager;

    try
    {
        const auto count = world.GetDimensions().TileCount();
        const int last = static_cast<int>(count) - 1;

        image = manager.CreateRgba(count, count, BuildZonePixels(world, {0, 0, last, last}), {TextureContent::Data});
        roadImage = manager.CreateRgba(count, count, BuildRoadPreviewPixels(world, {0, 0, last, last}), {TextureContent::Data});
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = SDL_CreateGPUSampler(device, &info);

        if (!sampler) 
        {
            throw std::runtime_error(std::string("Create zone sampler: ") + SDL_GetError());
        }
    }
    catch (...) 
    { 
        Release(); 
        throw; 
    }
}

void ZoneOverlay::Update(const World& world, ToolTileRect bounds)
{
    preparedRoadPath.reset();
    textures->UpdateRgba(*image, bounds.minX, bounds.minZ, bounds.maxX - bounds.minX + 1, bounds.maxZ - bounds.minZ + 1, BuildZonePixels(world, bounds));
    textures->UpdateRgba(*roadImage, bounds.minX, bounds.minZ, bounds.maxX - bounds.minX + 1, bounds.maxZ - bounds.minZ + 1, BuildRoadPreviewPixels(world, bounds));
}

void ZoneOverlay::PrepareRoadPreview(const World& world, const ToolPreview& preview)
{
    if (!preview.roadPath) return;
    const auto& path = *preview.roadPath;
    if (preparedWorld == &world && preparedRoadRevision == world.GetRoadRevision() && preparedRoadPath == path) return;
    // A failed partial upload must not leave the old preview marked as cached.
    preparedRoadPath.reset();
    const auto plan = world.PlanRoads(path);
    // Upload small blocks along either kind of route. G holds acceptance and B
    // holds footprint sides. Off-path values are never sampled by the shader.
    for (std::size_t first = 0; first < plan.tiles.size(); first += 16)
    {
        const auto& a = plan.tiles[first];
        const auto& b = plan.tiles[std::min(first + 15, plan.tiles.size() - 1)];
        const auto r = ToolTileRect::FromCorners({a.x, a.z}, {b.x, b.z});
        textures->UpdateRgba(*roadImage, r.minX, r.minZ, r.maxX - r.minX + 1, r.maxZ - r.minZ + 1,
            BuildPlannedRoadPreviewPixels(world, plan, r));
    }
    preparedRoadPath = path;
    preparedWorld = &world;
    preparedRoadRevision = world.GetRoadRevision();
}

void ZoneOverlay::Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ToolPreview& preview, const ZoneEvaluation& evaluation, Uint32 samplerSlot, Uint32 uniformSlot) const
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
    else if (preview.tiles && !preview.zoneType)
    {
        // All non-zone tools use the same palette, with their own placement
        // rules. Their preview may cover one tile or a rectangular footprint.
        const auto r = *preview.tiles;
        const auto color = preview.placementAllowed ? ValidToolPreviewColor : InvalidToolPreviewColor;
        uniforms.previewEnabled = 2;
        uniforms.minX = static_cast<float>(r.minX); uniforms.minZ = static_cast<float>(r.minZ);
        uniforms.maxX = static_cast<float>(r.maxX); uniforms.maxZ = static_cast<float>(r.maxZ);
        uniforms.red = color.r;
        uniforms.green = color.g;
        uniforms.blue = color.b;
        if (preview.roadPath)
        {
            const auto& path = *preview.roadPath;
            uniforms.previewEnabled = 3;
            uniforms.startX = static_cast<float>(path.startX); uniforms.startZ = static_cast<float>(path.startZ);
            uniforms.endX = static_cast<float>(path.endX); uniforms.endZ = static_cast<float>(path.endZ);
            // Road route: 0=Z-first L, 1=X-first L, 2=straight diagonal.
            uniforms.zoneType = path.diagonal ? 2.0f : path.xFirst ? 1.0f : 0.0f;
            uniforms.red = ValidToolPreviewColor.r;
            uniforms.green = ValidToolPreviewColor.g;
            uniforms.blue = ValidToolPreviewColor.b;
        }
    }

    const SDL_GPUTextureSamplerBinding bindings[] = {{image->Handle(), sampler}, {roadImage->Handle(), sampler}};
    SDL_BindGPUFragmentSamplers(pass, samplerSlot, bindings, 2);
    SDL_PushGPUFragmentUniformData(commands, uniformSlot, &uniforms, sizeof(uniforms));
}

void ZoneOverlay::Release() noexcept
{
    image.reset();
    roadImage.reset();

    if (sampler) 
    {
        SDL_ReleaseGPUSampler(device, sampler);
    }

    sampler = nullptr;
    textures = nullptr;
    device = nullptr;
    preparedRoadPath.reset();
    preparedWorld = nullptr;
    preparedRoadRevision = 0;
}
