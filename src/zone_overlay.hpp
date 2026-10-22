#pragma once

#include "texture.hpp"
#include "world_zoning.hpp"

struct alignas(16) ZoneOverlayUniforms
{
    float mapSize = 0, opacity = 0, previewEnabled = 0, affordable = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    float red = 0, green = 0, blue = 0, zoneType = 0;
    float startX = 0, startZ = 0, endX = 0, endZ = 0;
};
static_assert(sizeof(ZoneOverlayUniforms) == 64);

// One texel per tile: memory and edit upload size stay small on giant maps.
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
    void Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ToolPreview& preview,
              const ZoneEvaluation& evaluation, Uint32 samplerSlot = 2, Uint32 uniformSlot = 1) const;
    void Release() noexcept;
private:
    SDL_GPUDevice* device = nullptr;
    TextureManager* textures = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    std::unique_ptr<Texture> image;
};
