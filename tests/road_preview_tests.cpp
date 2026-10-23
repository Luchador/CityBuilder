#include "zone_overlay.hpp"
#include "world.hpp"
#include "road_footprint.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

// Texture/GPU doubles exercise real preview preparation and cache invalidation.
struct SDL_GPUDevice {};
struct SDL_GPUCommandBuffer {};
struct SDL_GPURenderPass {};
struct SDL_GPUSampler {};
struct SDL_GPUTexture { Uint32 width, height; std::vector<Uint8> pixels; };
namespace
{
    int texturesAlive = 0, samplersAlive = 0, updates = 0, failAfter = -1;
    Uint32 largestPatch = 0;
    SDL_GPUTexture* roadImage = nullptr;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void CheckPreview(const World& world, const ToolPreview& preview)
    {
        const auto plan = world.PlanRoads(*preview.roadPath);
        for (const auto& tile : plan.tiles)
        {
            Check(roadImage->pixels[(tile.z * roadImage->width + tile.x) * 4 + 1] ==
                  (RoadPlacementAccepted(tile.status) ? 255 : 0), "Uploaded preview disagrees with the placement plan");
            const auto offset = (tile.z * roadImage->width + tile.x) * 4;
            const auto mask = roadImage->pixels[offset + 2] | (roadImage->pixels[offset] << 8) |
                (roadImage->pixels[offset + 3] << 16);
            Check(static_cast<std::uint32_t>(mask) == RoadFootprintMask(tile.diagonalAxis, tile.mixedConnections),
                "Wrong planned footprint mask");
        }
    }
}

const char* SDL_GetError() { return "Preview test failure"; }
Texture::~Texture() { delete handle; --texturesAlive; }
void TextureManager::Initialize(SDL_GPUDevice* gpu) { device = gpu; }
void TextureManager::Release() noexcept { textures.clear(); device = nullptr; }
std::unique_ptr<Texture> TextureManager::CreateRgba(Uint32 width, Uint32 height, std::span<const Uint8> pixels, TextureOptions options)
{
    Check(options.content == TextureContent::Data && pixels.size() == width * height * 4, "Preview texture format mismatch");
    std::unique_ptr<Texture> result(new Texture(device));
    result->width = width; result->height = height; result->content = options.content;
    result->handle = new SDL_GPUTexture{width, height, {pixels.begin(), pixels.end()}};
    ++texturesAlive;
    if (texturesAlive == 2) roadImage = result->handle;
    return result;
}
void TextureManager::UpdateRgba(Texture& texture, Uint32 x, Uint32 y, Uint32 width, Uint32 height, std::span<const Uint8> pixels)
{
    Check(x + width <= texture.width && y + height <= texture.height && pixels.size() == width * height * 4,
          "Preview upload escaped its texture");
    if (failAfter == 0) { failAfter = -1; throw std::runtime_error("Injected preview upload failure"); }
    if (failAfter > 0) --failAfter;
    for (Uint32 row = 0; row < height; ++row)
        std::copy_n(pixels.begin() + row * width * 4, width * 4,
            texture.handle->pixels.begin() + ((y + row) * texture.width + x) * 4);
    ++updates;
    largestPatch = std::max(largestPatch, width * height);
}
SDL_GPUSampler* SDL_CreateGPUSampler(SDL_GPUDevice*, const SDL_GPUSamplerCreateInfo*) { ++samplersAlive; return new SDL_GPUSampler; }
void SDL_ReleaseGPUSampler(SDL_GPUDevice*, SDL_GPUSampler* sampler) { delete sampler; --samplersAlive; }
void SDL_BindGPUFragmentSamplers(SDL_GPURenderPass*, Uint32 first, const SDL_GPUTextureSamplerBinding* binding, Uint32 count)
{
    Check(first == 2 && count == 2 && binding[1].texture == roadImage, "Preview bound the wrong texture slots");
}
void SDL_PushGPUFragmentUniformData(SDL_GPUCommandBuffer*, Uint32 slot, const void* data, Uint32 size)
{
    Check(slot == 1 && size == sizeof(ZoneOverlayUniforms), "Preview uniform layout changed");
    ZoneOverlayUniforms values;
    std::memcpy(&values, data, size);
    Check(values.previewEnabled == 3 && values.zoneType == 2, "Diagonal preview mode was lost");
}

int main()
{
    try
    {
        TerrainSettings settings;
        settings.dimensions = MapDimensions(128); settings.hillHeight = 0; settings.lakes.count = 0;
        World world(settings);
        world.PlaceRoads({3, 3, 11, 11, true, true});
        SDL_GPUDevice device;
        SDL_GPUCommandBuffer commands;
        SDL_GPURenderPass pass;
        TextureManager textures;
        textures.Initialize(&device);
        ZoneOverlay overlay;
        overlay.Initialize(&device, textures, world);
        ToolPreview preview;
        preview.roadPath = RoadPath{3, 11, 7, 7, true, true};
        preview.tiles = ToolTileRect{2, 6, 8, 12};
        overlay.PrepareRoadPreview(world, preview);
        overlay.Bind(&commands, &pass, preview, {});
        CheckPreview(world, preview);
        const int cached = updates;
        overlay.PrepareRoadPreview(world, preview);
        Check(updates == cached, "Unchanged preview reuploaded");

        world.GetTile(6, 8).buildingId = 7;
        overlay.Update(world, {6, 8, 6, 8});
        overlay.PrepareRoadPreview(world, preview);
        CheckPreview(world, preview);
        Check(roadImage->pixels[(7 * 128 + 7) * 4 + 1] == 0, "Blocked T approach retained a valid junction preview");
        world.GetTile(6, 8).buildingId = NoBuilding;
        overlay.Update(world, {6, 8, 6, 8});
        overlay.PrepareRoadPreview(world, preview);
        CheckPreview(world, preview);
        const int beforeCommit = updates;
        world.PlaceRoads(*preview.roadPath);
        overlay.PrepareRoadPreview(world, preview);
        Check(updates > beforeCommit, "Road revision failed to invalidate the preview cache");
        CheckPreview(world, preview);

        auto longPreview = preview;
        longPreview.roadPath = RoadPath{1, 120, 120, 1, true, true};
        failAfter = 1;
        bool failed = false;
        try { overlay.PrepareRoadPreview(world, longPreview); } catch (const std::runtime_error&) { failed = true; }
        Check(failed, "Failure injection was not exercised");
        const int afterFailure = updates;
        overlay.PrepareRoadPreview(world, preview);
        Check(updates > afterFailure, "Partial failed upload left a stale cache hit");
        CheckPreview(world, preview);
        largestPatch = 0;
        const int beforeLong = updates;
        overlay.PrepareRoadPreview(world, longPreview);
        CheckPreview(world, longPreview);
        Check(updates - beforeLong == 8 && largestPatch <= 16 * 16,
              "Long diagonal uploaded a map-sized preview rectangle");
        world.PlaceRoads({30, 30, 40, 30});
        ToolPreview mixed;
        mixed.roadPath = RoadPath{35, 30, 39, 26, true, true};
        overlay.PrepareRoadPreview(world, mixed);
        CheckPreview(world, mixed);
        auto center = (30 * 128 + 35) * 4;
        Check(roadImage->pixels[center + 1] == 255 && (roadImage->pixels[center + 2] & 15) == (RoadNorth | RoadEast),
              "Mixed T preview did not reserve the correct shoulders");
        // Grid-NE owns E and NE, with the NE pavement's two far shoulders.
        // These exceed the old four side bits and cross the B/R byte boundary.
        Check(roadImage->pixels[center+2] == 0x86 && roadImage->pixels[center] == 0x18 &&
              roadImage->pixels[center+3] == 0, "Three-tile preview omitted a diagonal cell or far shoulder");
        world.GetTile(36, 29).buildingId = 22;
        overlay.Update(world, {36, 29, 36, 29});
        overlay.PrepareRoadPreview(world, mixed);
        CheckPreview(world, mixed);
        Check(roadImage->pixels[center + 1] == 0, "Mixed T preview ignored blocked approach");
        world.GetTile(36, 29).buildingId = NoBuilding;
        overlay.Update(world, {36, 29, 36, 29});
        Check(world.PlaceRoads(*mixed.roadPath).evaluation.upgrades == 1, "Mixed T commit failed");
        mixed.roadPath = RoadPath{35, 30, 31, 34, true, true};
        overlay.PrepareRoadPreview(world, mixed);
        CheckPreview(world, mixed);
        Check(roadImage->pixels[center + 1] == 255 && (roadImage->pixels[center + 2] & 15) == 15,
              "Mixed cross preview did not reserve all shoulders");
        Check(roadImage->pixels[center+3] != 0, "Five-tile preview lost the high footprint byte");
        // A diagonal through route gains two perpendicular grid approaches.
        Check(!world.PlaceRoads({51,51,59,59,true,true}).evaluation.blocked &&
              !world.PlaceRoads({55,55,51,55}).evaluation.blocked,"Arrow preview fixture failed");
        ToolPreview arrow;
        arrow.roadPath=RoadPath{55,55,55,51};
        overlay.PrepareRoadPreview(world,arrow);
        CheckPreview(world,arrow);
        const int arrowCenter=(55*128+55)*4;
        Check(roadImage->pixels[arrowCenter+1]==255 && roadImage->pixels[arrowCenter+2]==0x2f &&
              roadImage->pixels[arrowCenter]==6 && roadImage->pixels[arrowCenter+3]==0,
              "Arrow preview omitted an owned cell or shoulder");
        world.GetTile(55,54).buildingId=123;
        overlay.Update(world,{55,54,55,54});
        overlay.PrepareRoadPreview(world,arrow);
        CheckPreview(world,arrow);
        Check(roadImage->pixels[arrowCenter+1]==0,"Arrow preview ignored a blocked approach");
        world.GetTile(55,54).buildingId=NoBuilding;
        overlay.Update(world,{55,54,55,54});
        Check(world.PlaceRoads(*arrow.roadPath).evaluation.upgrades==1 &&
              GetRoadVisual(*world.GetRoadAt(55,55)).shape==RoadShape::MixedFourWayArrow,
              "Arrow preview/commit mismatch");
        world.PlaceRoads({80, 80, 84, 76, true, true});
        ToolPreview curve;
        curve.roadPath = RoadPath{80, 80, 84, 84, true, true};
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        center = (80 * 128 + 80) * 4;
        Check(roadImage->pixels[center + 1] == 255 && (roadImage->pixels[center + 2] & 15) == 15,
              "Diagonal curve preview missing or wrong footprint");
        world.GetTile(81, 81).buildingId = 33;
        overlay.Update(world, {81, 81, 81, 81});
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        Check(roadImage->pixels[center + 1] == 0, "Curve preview ignored a blocked approach");
        world.GetTile(81, 81).buildingId = NoBuilding;
        overlay.Update(world, {81, 81, 81, 81});
        Check(world.PlaceRoads(*curve.roadPath).evaluation.upgrades == 1, "Curve preview did not match commit");
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        // Reuse the corner above: inside T, then through-road intersection.
        curve.roadPath = RoadPath{80, 80, 84, 80};
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        Check(roadImage->pixels[center + 1] == 255 &&
              roadImage->pixels[center + 2] == (RoadNorth | RoadEast | RoadSouth),
              "Inside corner junction preview has the wrong footprint");
        world.GetTile(81, 80).buildingId = 44;
        overlay.Update(world, {81, 80, 81, 80});
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        Check(roadImage->pixels[center + 1] == 0, "Inside corner junction preview ignored obstruction");
        world.GetTile(81, 80).buildingId = NoBuilding;
        overlay.Update(world, {81, 80, 81, 80});
        Check(world.PlaceRoads(*curve.roadPath).evaluation.upgrades == 1, "Inside junction preview/commit mismatch");
        curve.roadPath = RoadPath{80, 80, 76, 80};
        overlay.PrepareRoadPreview(world, curve);
        CheckPreview(world, curve);
        Check(roadImage->pixels[center + 1] == 255 &&
              roadImage->pixels[center + 2] == (RoadNorth | RoadEast | RoadSouth),
              "Double corner junction preview has the wrong footprint");
        Check(world.PlaceRoads(*curve.roadPath).evaluation.upgrades == 1, "Double junction preview/commit mismatch");
        Check(world.GetRoadModuleAt(81,80) == world.GetRoadAt(80,80) &&
              world.GetRoadAllowedConnections(81,80) == (RoadWest|RoadEast),
              "Covered cell did not expose the module's straight ports");
        curve.roadPath = RoadPath{81,80,81,79};
        overlay.PrepareRoadPreview(world,curve);
        CheckPreview(world,curve);
        Check(roadImage->pixels[(80*128+81)*4+1] == 0, "Covered inside tile allowed an independent sideways turn");
        overlay.Release(); overlay.Release();
        Check(!texturesAlive && !samplersAlive, "Preview resources leaked");
        std::cout << "PASS: uploaded junction preview, obstacle/revision invalidation, cache, bounded batches, failed-upload retry and cleanup\n";
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
