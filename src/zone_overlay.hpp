#pragma once

#include "texture.hpp"
#include "world_zoning.hpp"

#include <cstdint>
#include <vector>

struct alignas(16) ZoneOverlayUniforms
{
    float mapSize = 0, opacity = 0, previewEnabled = 0, affordable = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    float red = 0, green = 0, blue = 0, zoneType = 0;
    float startX = 0, startZ = 0, endX = 0, endZ = 0;
    float bendX = 0, bendZ = 0, lotPreviewEnabled = 0, padding1 = 0;
};
static_assert(sizeof(ZoneOverlayUniforms) == 80);

// Three maps with one texel per tile: zones, road eligibility and lot markings.
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
    // Non-mutating lot plan. Call each frame before acquiring the command buffer;
    // unchanged requests reuse the previous upload, including while stationary.
    void PrepareLotPreview(const World& world, const ToolPreview& preview, const ZoneEvaluation& evaluation);
    // Binds zones, roads, lot metadata and arrow graphic in four consecutive slots.
    void Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ToolPreview& preview,
              const ZoneEvaluation& evaluation, Uint32 samplerSlot = 2, Uint32 uniformSlot = 1) const;
    void Release() noexcept;
private:
    std::vector<std::uint8_t> CopyCommittedLotRegion(ToolTileRect bounds) const;
    void RestoreLotPreview();
    SDL_GPUDevice* device = nullptr;
    TextureManager* textures = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUSampler* arrowSampler = nullptr;
    std::unique_ptr<Texture> image;
    std::unique_ptr<Texture> roadImage;
    std::unique_ptr<Texture> lotImage;
    const Texture* arrowImage = nullptr;
    std::uint64_t preparedLotRevision = 0;
    std::vector<std::uint8_t> committedLotPixels;
    std::optional<ZoneRequest> preparedLotRequest;
    ToolTileRect preparedLotBounds{}; // Includes remnants outside the gesture.
    const World* preparedLotWorld = nullptr;
    std::uint64_t previewLotRevision = 0, previewRoadRevision = 0;
    std::optional<RoadPath> preparedRoadPath;
    const World* preparedWorld = nullptr;
    std::uint64_t preparedRoadRevision = 0;
};
