#pragma once

#include "texture.hpp"
#include "world_zoning.hpp"

struct alignas(16) ZoneOverlayUniforms
{
    float mapSize = 0, opacity = 0, previewEnabled = 0, affordable = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    float red = 0, green = 0, blue = 0, zoneType = 0;
    float startX = 0, startZ = 0, endX = 0, endZ = 0;
    float bendX = 0, bendZ = 0, padding0 = 0, padding1 = 0;
};
static_assert(sizeof(ZoneOverlayUniforms) == 80);

// Two maps with one texel per tile: zone data and direction-aware road eligibility.
class ZoneOverlay
{
public:
    ~ZoneOverlay() { Release(); }
    ZoneOverlay() = default;
    ZoneOverlay(const ZoneOverlay&) = delete;
    ZoneOverlay& operator=(const ZoneOverlay&) = delete;
    void Initialize(SDL_GPUDevice* gpu, TextureManager& textures, const World& world);
    // Also call after future occupancy/height edits to refresh eligibility.
    void Update(const World& world, ToolTileRect bounds);
    // Upload a changed road plan before acquiring the frame command buffer.
    // Both legs can contain transitions or junctions with wider footprints.
    void PrepareRoadPreview(const World& world, const ToolPreview& preview);
    // Binds zone data at samplerSlot and road eligibility at samplerSlot + 1.
    void Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ToolPreview& preview,
              const ZoneEvaluation& evaluation, Uint32 samplerSlot = 2, Uint32 uniformSlot = 1) const;
    void Release() noexcept;
private:
    SDL_GPUDevice* device = nullptr;
    TextureManager* textures = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    std::unique_ptr<Texture> image;
    std::unique_ptr<Texture> roadImage;
    std::optional<RoadPath> preparedRoadPath;
    const World* preparedWorld = nullptr;
    std::uint64_t preparedRoadRevision = 0;
};
