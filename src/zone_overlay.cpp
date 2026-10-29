#include "zone_overlay.hpp"
#include "zone_render_data.hpp"
#include "tool.hpp"
#include "world.hpp"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <utility>

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
        committedLotPixels = BuildLotPixels(world);
        lotImage = manager.CreateRgba(count, count, committedLotPixels, {TextureContent::Data});
        arrowImage = &manager.Load("materials/zones/zone_arrow.png", {TextureContent::Color});
        preparedLotRevision = world.GetLotRevision();
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = SDL_CreateGPUSampler(device, &info);

        if (!sampler) 
        {
            throw std::runtime_error(std::string("Create zone sampler: ") + SDL_GetError());
        }
        info.min_filter = info.mag_filter = SDL_GPU_FILTER_LINEAR;
        arrowSampler = SDL_CreateGPUSampler(device, &info);
        if (!arrowSampler) throw std::runtime_error(std::string("Create lot arrow sampler: ") + SDL_GetError());
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
    if (preparedLotRevision != world.GetLotRevision())
    {
        // A partial edit can reform lots extending outside this rectangle.
        // Refresh only when lots change; never rebuild this map per frame.
        const auto count = world.GetDimensions().TileCount();
        auto pixels = BuildLotPixels(world);
        textures->UpdateRgba(*lotImage, 0, 0, count, count, pixels);
        committedLotPixels = std::move(pixels);
        preparedLotRevision = world.GetLotRevision();
        preparedLotRequest.reset();
        preparedLotWorld = nullptr;
    }
    else RestoreLotPreview();
}

std::vector<std::uint8_t> ZoneOverlay::CopyCommittedLotRegion(ToolTileRect bounds) const
{
    const auto width = static_cast<std::size_t>(bounds.maxX - bounds.minX + 1);
    const auto height = static_cast<std::size_t>(bounds.maxZ - bounds.minZ + 1);
    std::vector<std::uint8_t> pixels(width * height * 4);
    for (std::size_t row = 0; row < height; ++row)
    {
        const auto offset = ((bounds.minZ + row) * image->Width() + bounds.minX) * 4;
        std::copy_n(committedLotPixels.begin() + offset, width * 4, pixels.begin() + row * width * 4);
    }
    return pixels;
}

void ZoneOverlay::RestoreLotPreview()
{
    if (!preparedLotRequest) return;
    const auto r = preparedLotBounds;
    textures->UpdateRgba(*lotImage, r.minX, r.minZ, r.maxX - r.minX + 1, r.maxZ - r.minZ + 1,
        CopyCommittedLotRegion(r));
    preparedLotRequest.reset();
    preparedLotWorld = nullptr;
}

void ZoneOverlay::PrepareLotPreview(const World& world, const ToolPreview& preview, const ZoneEvaluation& evaluation)
{
    if (!preview.tiles || !preview.zoneType ||
        !evaluation.valid || !evaluation.affordable)
    {
        RestoreLotPreview();
        return;
    }
    const ZoneRequest request{*preview.tiles, *preview.zoneType, preview.useMaximumLotSize};
    if (preparedLotWorld == &world && preparedLotRequest == request &&
        previewLotRevision == world.GetLotRevision() && previewRoadRevision == world.GetRoadRevision()) return;

    RestoreLotPreview();
    // Both plans are shared with ApplyZoning. Include complete affected lots,
    // since their remnants can extend beyond the selected rectangle.
    const auto lots = PlanZoneLots(world, request);
    const auto repairs = PlanZoningLotRepairs(world, request);
    auto r = request.tiles;
    for (const auto& lot : repairs.removed)
    {
        r.minX = std::min(r.minX, lot.x); r.minZ = std::min(r.minZ, lot.z);
        r.maxX = std::max(r.maxX, lot.x + static_cast<int>(lot.width) - 1);
        r.maxZ = std::max(r.maxZ, lot.z + static_cast<int>(lot.depth) - 1);
    }
    auto pixels = CopyCommittedLotRegion(r);
    const auto width = static_cast<std::size_t>(r.maxX - r.minX + 1);
    const auto height = static_cast<std::size_t>(r.maxZ - r.minZ + 1);
    for (const auto& lot : repairs.removed)
        for (int z = lot.z; z < lot.z + static_cast<int>(lot.depth); ++z)
    {
        const auto i = (static_cast<std::size_t>(z - r.minZ) * width + lot.x - r.minX) * 4;
        std::fill_n(pixels.begin() + i, static_cast<std::size_t>(lot.width) * 4, 0);
    }
    const auto paste = [&](std::span<const Lot> planned, bool newZoning)
    {
        if (planned.empty()) return;
        const auto metadata = BuildLotPreviewPixels(planned, r);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            if (!(metadata[i] & LotPreviewTileBit)) continue;
            std::copy_n(metadata.begin() + i, 4, pixels.begin() + i);
            // Remnants keep their original zone color, outside or inside the
            // gesture. Only newly zoned lots use the new-zone preview tint.
            if (!newZoning) pixels[i] &= static_cast<std::uint8_t>(~LotPreviewTileBit);
        }
    };
    paste(repairs.replacements, false);
    paste(lots, true);
    textures->UpdateRgba(*lotImage, r.minX, r.minZ, static_cast<Uint32>(width), static_cast<Uint32>(height), pixels);
    preparedLotRequest = request;
    preparedLotBounds = r;
    preparedLotWorld = &world;
    previewLotRevision = world.GetLotRevision();
    previewRoadRevision = world.GetRoadRevision();
}

void ZoneOverlay::PrepareRoadPreview(const World& world, const ToolPreview& preview)
{
    if (!preview.roadPath) return;
    const auto& path = *preview.roadPath;
    if (preparedWorld == &world && preparedRoadRevision == world.GetRoadRevision() && preparedRoadPath == path) return;
    // A failed partial upload must not leave the old preview marked as cached.
    preparedRoadPath.reset();
    const auto plan = world.PlanRoads(path);
    // Upload small blocks along either kind of route. G holds acceptance;
    // B/R/A hold the 24 footprint bits. Off-path values are never sampled.
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
        const ZoneRequest request{r, *preview.zoneType, preview.useMaximumLotSize};
        uniforms.lotPreviewEnabled = evaluation.affordable && preparedLotRequest == request ? 1.0f : 0.0f;
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
            const auto bend = path.Bend();
            uniforms.bendX = static_cast<float>(bend.x); uniforms.bendZ = static_cast<float>(bend.z);
            uniforms.red = ValidToolPreviewColor.r;
            uniforms.green = ValidToolPreviewColor.g;
            uniforms.blue = ValidToolPreviewColor.b;
        }
    }

    const SDL_GPUTextureSamplerBinding bindings[] = {{image->Handle(), sampler}, {roadImage->Handle(), sampler},
        {lotImage->Handle(), sampler}, {arrowImage->Handle(), arrowSampler}};
    SDL_BindGPUFragmentSamplers(pass, samplerSlot, bindings, 4);
    SDL_PushGPUFragmentUniformData(commands, uniformSlot, &uniforms, sizeof(uniforms));
}

void ZoneOverlay::Release() noexcept
{
    image.reset();
    roadImage.reset();
    lotImage.reset();
    arrowImage = nullptr;
    if (arrowSampler) SDL_ReleaseGPUSampler(device, arrowSampler);
    arrowSampler = nullptr;
    preparedLotRevision = 0;
    committedLotPixels.clear();
    preparedLotRequest.reset();
    preparedLotWorld = nullptr;
    previewLotRevision = previewRoadRevision = 0;

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
