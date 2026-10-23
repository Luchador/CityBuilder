#include "road_renderer.hpp"
#include "road_mesh.hpp"
#include "gpu_utils.hpp"
#include "world.hpp"

#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

// SDL/GPU doubles check bindings, instance uploads and partial-failure cleanup.
// These are CPU checks, not a substitute for a Windows/D3D12 visual run.
struct SDL_GPUDevice {};
struct SDL_GPURenderPass {};
struct SDL_GPUShader {};
struct SDL_GPUSampler {};
struct SDL_GPUGraphicsPipeline {};
struct SDL_GPUTexture { std::string path; };
struct SDL_GPUBuffer { std::vector<Uint8> bytes; std::size_t shape = RoadAssets.size(); };
namespace
{
    int buffers = 0, shaders = 0, samplers = 0, pipelines = 0, uploads = 0, draws = 0, fallbackTextures = 0;
    constexpr int MeshBuffers = static_cast<int>(RoadAssets.size() * 2);
    std::array<StaticMesh, RoadAssets.size()> expectedMeshes;
    std::array<std::string, RoadAssets.size()> expectedTextures;
    bool failPipeline = false, failUpload = false;
    Uint32 drawnInstances = 0;
    SDL_GPUBuffer* boundInstances = nullptr;
    SDL_GPUBuffer* boundIndices = nullptr;
    SDL_GPUBuffer* boundVertices = nullptr;
    Uint32 instanceOffset = 0;
    SDL_GPUTexture* boundTexture = nullptr;
    std::array<Uint32, static_cast<std::size_t>(RoadShape::Count)> shapeInstances{};
    std::vector<float> instanceTurns;
    std::string basePath;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    template<class F> void Throws(F operation)
    {
        bool threw = false;
        try { operation(); } catch (const std::exception&) { threw = true; }
        Check(threw, "Expected injected failure");
    }
    void CheckReleased()
    {
        Check(!buffers && !shaders && !samplers && !pipelines && !fallbackTextures, "Road GPU resource leaked");
    }
}

const char* SDL_GetBasePath() { return basePath.c_str(); }
const char* SDL_GetError() { return "Injected GPU failure"; }
Texture::~Texture() { if (handle && handle->path == "plain-asphalt") --fallbackTextures; delete handle; }
void TextureManager::Initialize(SDL_GPUDevice* gpu) { device = gpu; }
void TextureManager::Release() noexcept { textures.clear(); device = nullptr; }
const Texture& TextureManager::Load(std::string_view path, TextureOptions options)
{
    Check(std::any_of(RoadAssets.begin(), RoadAssets.end(), [&](const auto& asset) { return path == asset.texture; }) &&
          options.content == TextureContent::Color && options.mipmaps,
          "Road must load its base color as mipmapped sRGB");
    auto& entry = textures[std::string(path)];
    if (!entry)
    {
        entry.reset(new Texture(device));
        entry->handle = new SDL_GPUTexture{std::string(path)};
        entry->mipLevels = 9;
    }
    return *entry;
}
std::unique_ptr<Texture> TextureManager::CreateRgba(Uint32 width, Uint32 height, std::span<const Uint8> pixels, TextureOptions options)
{
    Check(width == 1 && height == 1 && pixels.size() == 4 && pixels[3] == 255 &&
          options.content == TextureContent::Color && !options.mipmaps, "Invalid asphalt fallback");
    std::unique_ptr<Texture> result(new Texture(device));
    result->handle = new SDL_GPUTexture{"plain-asphalt"};
    result->width = width; result->height = height;
    ++fallbackTextures;
    return result;
}
SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice*, const void* data, Uint32 size, SDL_GPUBufferUsageFlags usage)
{
    if (failUpload) throw std::runtime_error("Injected upload failure");
    const auto* bytes = static_cast<const Uint8*>(data);
    auto* buffer = new SDL_GPUBuffer{{bytes, bytes + size}};
    if (usage == SDL_GPU_BUFFERUSAGE_VERTEX)
        for (std::size_t i = 0; i < expectedMeshes.size(); ++i)
            if (size == expectedMeshes[i].vertices.size() * sizeof(StaticMeshVertex) &&
                std::memcmp(data, expectedMeshes[i].vertices.data(), size) == 0) { buffer->shape = i; break; }
    ++buffers; ++uploads;
    return buffer;
}
SDL_GPUShader* CompileShader(SDL_GPUDevice*, const char*, SDL_GPUShaderStage stage, Uint32 samplerCount,
                             const char* preamble, Uint32 uniformCount)
{
    Check(preamble && *preamble && uniformCount == (stage == SDL_GPU_SHADERSTAGE_VERTEX ? 0u : 1u) &&
          samplerCount == (stage == SDL_GPU_SHADERSTAGE_VERTEX ? 0u : 3u), "Incorrect road shader resources");
    ++shaders;
    return new SDL_GPUShader;
}
SDL_GPUSampler* SDL_CreateGPUSampler(SDL_GPUDevice*, const SDL_GPUSamplerCreateInfo* info)
{
    Check(info->min_filter == SDL_GPU_FILTER_LINEAR && info->max_lod == 8, "Missing mipmap sampler");
    ++samplers;
    return new SDL_GPUSampler;
}
SDL_GPUGraphicsPipeline* SDL_CreateGPUGraphicsPipeline(SDL_GPUDevice*, const SDL_GPUGraphicsPipelineCreateInfo* info)
{
    Check(info->depth_stencil_state.enable_depth_test && info->depth_stencil_state.enable_depth_write &&
          info->rasterizer_state.cull_mode == SDL_GPU_CULLMODE_BACK &&
          info->rasterizer_state.front_face == SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE &&
          !info->target_info.color_target_descriptions[0].blend_state.enable_blend, "Incorrect opacity/depth/culling");
    const auto& input = info->vertex_input_state;
    Check(input.num_vertex_buffers == 2 && input.num_vertex_attributes == 5 &&
          input.vertex_buffer_descriptions[0].pitch == 32 && input.vertex_buffer_descriptions[1].pitch == 32 &&
          input.vertex_buffer_descriptions[1].input_rate == SDL_GPU_VERTEXINPUTRATE_INSTANCE &&
          input.vertex_buffer_descriptions[1].instance_step_rate == 0 && input.vertex_attributes[3].buffer_slot == 1 &&
          input.vertex_attributes[3].format == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 &&
          input.vertex_attributes[4].buffer_slot == 1 && input.vertex_attributes[4].offset == 16 &&
          input.vertex_attributes[4].format == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
          "Incorrect instanced vertex layout");
    if (failPipeline) return nullptr;
    ++pipelines;
    return new SDL_GPUGraphicsPipeline;
}
void SDL_ReleaseGPUBuffer(SDL_GPUDevice*, SDL_GPUBuffer* buffer) { delete buffer; --buffers; }
void SDL_ReleaseGPUShader(SDL_GPUDevice*, SDL_GPUShader* shader) { delete shader; --shaders; }
void SDL_ReleaseGPUSampler(SDL_GPUDevice*, SDL_GPUSampler* sampler) { delete sampler; --samplers; }
void SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice*, SDL_GPUGraphicsPipeline* pipeline) { delete pipeline; --pipelines; }
void SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass*, SDL_GPUGraphicsPipeline* pipeline) { Check(pipeline, "Null pipeline"); }
void SDL_BindGPUVertexBuffers(SDL_GPURenderPass*, Uint32 first, const SDL_GPUBufferBinding* binding, Uint32 count)
{
    Check(first == 0 && count == 2 && binding[0].buffer && binding[1].buffer, "Missing mesh or instance binding");
    boundInstances = binding[1].buffer;
    boundVertices = binding[0].buffer;
    instanceOffset = binding[1].offset;
}
void SDL_BindGPUIndexBuffer(SDL_GPURenderPass*, const SDL_GPUBufferBinding* binding, SDL_GPUIndexElementSize size)
{
    Check(binding->buffer && size == SDL_GPU_INDEXELEMENTSIZE_32BIT, "Incorrect road index binding");
    boundIndices = binding->buffer;
}
void SDL_BindGPUFragmentSamplers(SDL_GPURenderPass*, Uint32 first, const SDL_GPUTextureSamplerBinding* binding, Uint32 count)
{
    Check(first == 0 && count == 1 && binding->texture && binding->sampler, "Incorrect base color binding");
    boundTexture = binding->texture;
}
void SDL_DrawGPUIndexedPrimitives(SDL_GPURenderPass*, Uint32 indices, Uint32 instances, Uint32 first, Sint32 offset, Uint32 firstInstance)
{
    Check(instances && !first && !offset && !firstInstance && boundIndices->bytes.size() == indices * 4 &&
          instanceOffset + instances * 32 <= boundInstances->bytes.size(), "Incorrect indexed road draw");
    const auto shape = boundVertices->shape;
    Check(shape < RoadAssets.size() && boundTexture->path == expectedTextures[shape], "Wrong mesh/texture pair");
    shapeInstances[shape] += instances;
    for (Uint32 i = 0; i < instances; ++i)
    {
        float values[4];
        std::memcpy(values, boundInstances->bytes.data() + instanceOffset + i * 32, sizeof(values));
        Check(values[3] >= 0 && values[3] <= 3.5f && std::floor(values[3] * 2) == values[3] * 2, "Invalid rotation in instance");
        instanceTurns.push_back(values[3]);
    }
    ++draws; drawnInstances += instances;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: CityRoadRendererTests project-directory");
        basePath = std::filesystem::absolute(argv[1]).string();
        for (std::size_t i = 0; i < RoadAssets.size(); ++i)
        {
            const auto& asset = RoadAssets[i];
            expectedMeshes[i] = LoadRoadMesh(std::filesystem::path(basePath) / "assets" / asset.model, asset);
            expectedTextures[i] = asset.mixedJunction && (!expectedMeshes[i].hasTexcoords ||
                !std::filesystem::exists(std::filesystem::path(basePath) / "assets" / asset.texture)) ?
                "plain-asphalt" : asset.texture;
        }
        SDL_GPUDevice device;
        SDL_GPURenderPass pass;
        TextureManager textures;
        textures.Initialize(&device);
        RoadRenderer renderer;
        renderer.Draw(&pass);
        Check(draws == 0, "Uninitialized renderer drew");
        failPipeline = true;
        Throws([&] { renderer.Initialize(&device, textures, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM); });
        Check(!renderer.IsInitialized(), "Failed renderer stayed initialized");
        CheckReleased();
        failPipeline = false;
        renderer.Initialize(&device, textures, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        TerrainSettings settings;
        settings.dimensions = MapDimensions(16); settings.hillHeight = 0; settings.lakes.count = 0;
        World world(settings);
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(draws == 0 && buffers == MeshBuffers, "Empty world allocated/drew instances");
        Check(world.PlaceRoad(3, 4), "Test road placement failed");
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == 1 && buffers == MeshBuffers + 1 && boundInstances->bytes.size() == 32 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::DeadEnd)] == 1, "Single road draw failed");
        float position[4];
        std::memcpy(position, boundInstances->bytes.data(), sizeof(position));
        Check(position[0] == 3.5f && position[2] == 4.5f && position[1] > TerrainBaseElevation &&
              position[1] < TerrainBaseElevation + .01f, "Road not centered/raised on terrain");
        const int previousUploads = uploads;
        renderer.Prepare(world);
        Check(uploads == previousUploads, "Unchanged frame reuploaded roads");
        Check(world.PlaceRoad(4, 4), "Second test road placement failed");
        failUpload = true;
        Throws([&] { renderer.Prepare(world); });
        drawnInstances = 0;
        renderer.Draw(&pass);
        Check(drawnInstances == 1 && buffers == MeshBuffers + 1, "Failed upload discarded previous render data");
        failUpload = false;
        drawnInstances = 0;
        instanceTurns.clear();
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == 2 && buffers == MeshBuffers + 1 && uploads == previousUploads + 1 &&
              instanceTurns == std::vector<float>{2, 0}, "Two ends did not face each other using shared instances");

        Check(world.PlaceRoads({4, 3, 4, 5, false}).changed == 2, "Cross fixture failed");
        Check(world.PlaceRoad(5, 4), "Cross fixture failed");
        Check(world.PlaceRoads({8, 8, 11, 9}).changed == 5 && world.PlaceRoad(9, 7), "Mixed shapes fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {}; instanceTurns.clear();
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == world.GetRoads().size() && draws == 5 && buffers == MeshBuffers + 1,
              "Five shapes were not batched into five draws/one instance buffer");
        for (std::size_t i = 0; i < 5; ++i) Check(shapeInstances[i] > 0, "A required shape was not drawn");
        const auto mixedUploads = uploads;
        renderer.Prepare(world);
        Check(uploads == mixedUploads, "Stable connected roads reuploaded");
        World sloped(settings);
        auto& terrain = const_cast<TerrainHeightmap&>(sloped.GetTerrain());
        for (unsigned z = 0; z <= 16; ++z) for (unsigned x = 0; x <= 16; ++x)
            terrain.heights[z * terrain.dimensions.CornerStride() + x] = 1.0f + .25f * z;
        Check(sloped.PlaceRoads({6, 2, 6, 4, false}).changed == 3, "Sloped renderer fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {};
        renderer.Prepare(sloped); renderer.Draw(&pass);
        Check(drawnInstances == 3 && draws == 2 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::Straight)] == 1 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::DeadEnd)] == 2,
              "Sloped roads did not share the straight/end batches");
        for (unsigned i = 0; i < 3; ++i)
        {
            float values[8];
            std::memcpy(values, boundInstances->bytes.data() + i * 32, sizeof(values));
            const auto* road = sloped.GetRoadAt(static_cast<int>(values[0]), static_cast<int>(values[2]));
            Check(road && values[4] == 0 && values[5] == .25f &&
                  std::abs(values[1] - road->surface.elevation - .001f) < .00001f &&
                  values[3] == GetRoadVisual(*road).quarterTurns,
                  "Slope, center elevation or orientation was lost in the GPU instance stream");
        }
        World diagonal(settings);
        Check(diagonal.PlaceRoads({2, 2, 5, 5, true, true}).changed == 4 &&
              diagonal.PlaceRoads({10, 3, 7, 6, true, true}).changed == 4, "Diagonal renderer fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {};
        renderer.Prepare(diagonal); renderer.Draw(&pass);
        Check(drawnInstances == 8 && draws == 2 && buffers == MeshBuffers + 1 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::Straight)] == 4 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::DeadEnd)] == 4,
              "Diagonals did not share the existing mesh batches");
        for (unsigned i = 0; i < 8; ++i)
        {
            float values[8];
            std::memcpy(values, boundInstances->bytes.data() + i * 32, sizeof(values));
            const auto* road = diagonal.GetRoadAt(static_cast<int>(values[0]), static_cast<int>(values[2]));
            Check(road && values[4] == 0 && values[5] == 0 && values[6] == RoadDiagonalLength &&
                  values[3] == GetRoadVisual(*road).quarterTurns &&
                  values[7] == (GetRoadVisual(*road).shape == RoadShape::Straight ? 1.0f : 0.0f),
                  "Diagonal orientation/scale/UV mode lost in instance stream");
        }
        World junction(settings);
        junction.PlaceRoads({3, 3, 11, 11, true, true});
        Check(junction.PlaceRoads({3, 11, 7, 7, true, true}).evaluation.upgrades == 1, "Renderer T fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {};
        renderer.Prepare(junction); renderer.Draw(&pass);
        Check(drawnInstances == 13 && draws == 3 && shapeInstances[static_cast<std::size_t>(RoadShape::DiagonalThreeWay)] == 1,
              "Diagonal T did not use its adapted mesh batch");
        const auto previousJunctionUploads = uploads;
        renderer.Prepare(junction);
        Check(uploads == previousJunctionUploads, "Unchanged junction reuploaded");
        Check(junction.PlaceRoads({7, 7, 11, 3, true, true}).evaluation.upgrades == 1, "Renderer cross fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {};
        renderer.Prepare(junction); renderer.Draw(&pass);
        Check(drawnInstances == 17 && draws == 3 && uploads == previousJunctionUploads + 1 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::DiagonalThreeWay)] == 0 &&
              shapeInstances[static_cast<std::size_t>(RoadShape::DiagonalFourWay)] == 1,
              "Junction upgrade retained the old batch or failed to invalidate its cache");
        bool foundJunction = false;
        for (unsigned i = 0; i < drawnInstances; ++i)
        {
            float values[8];
            std::memcpy(values, boundInstances->bytes.data() + i * 32, sizeof(values));
            if (values[0] == 7.5f && values[2] == 7.5f)
            {
                foundJunction = true;
                Check(values[3] == .5f && values[6] == 1 && values[7] == 0,
                      "Junction was stretched twice or used the straight's repeating texture mode");
            }
        }
        Check(foundJunction, "Junction center missing from the instance buffer");
        for (const auto& asset : RoadAssets) if (asset.mixedJunction) for (unsigned turn = 0; turn < 4; ++turn)
        {
            World mixed(settings);
            const auto mask = RotateRoadConnections(asset.connections, turn);
            int through = -1;
            constexpr int dx[]{-1,0,1,0,-1,1,1,-1}, dz[]{0,-1,0,1,-1,-1,1,1};
            for (int d = 0; d < 8; ++d)
                if ((mask & (1 << d)) && (mask & RotateRoadConnections(static_cast<std::uint8_t>(1 << d), 2)))
                { through = d; break; }
            Check(through >= 0, "Mixed fixture needs through route");
            mixed.PlaceRoads({7 - 3 * dx[through], 7 - 3 * dz[through], 7 + 3 * dx[through], 7 + 3 * dz[through], true, through >= 4});
            for (int d = 0; d < 8; ++d) if (mask & (1 << d))
                mixed.PlaceRoads({7, 7, 7 + 3 * dx[d], 7 + 3 * dz[d], true, d >= 4});
            Check(mixed.GetRoadAt(7, 7)->mixedConnections == mask, "Mixed renderer fixture failed");
            drawnInstances = 0; draws = 0; shapeInstances = {};
            // Use a fresh renderer for this stack-allocated world: pointer and
            // revision cache keys are intentionally scoped to a world's lifetime.
            RoadRenderer mixedRenderer;
            mixedRenderer.Initialize(&device, textures, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
            mixedRenderer.Prepare(mixed); mixedRenderer.Draw(&pass);
            const auto visual = GetRoadVisual(*mixed.GetRoadAt(7, 7));
            Check(shapeInstances[static_cast<std::size_t>(visual.shape)] == 1 && drawnInstances == mixed.GetRoads().size(),
                  "Mixed junction mesh batch missing");
            bool found = false;
            for (unsigned i = 0; i < drawnInstances; ++i)
            {
                float values[8]; std::memcpy(values, boundInstances->bytes.data() + i * 32, sizeof(values));
                if (values[0] == 7.5f && values[2] == 7.5f)
                {
                    found = true;
                    Check(values[3] == visual.quarterTurns && values[6] == 1 && values[7] == 0,
                          "Mixed junction got an extra 45-degree rotation, stretch or UV repeat");
                }
            }
            Check(found, "Mixed center missing from instance buffer");
        }
        for (int turn = 0; turn < 4; ++turn)
        {
            World curve(settings);
            constexpr int dx[]{-1, 1, 1, -1}, dz[]{-1, -1, 1, 1};
            for (int d : {(turn + 1) % 4, (turn + 2) % 4})
                Check(!curve.PlaceRoads({7, 7, 7 + 3 * dx[d], 7 + 3 * dz[d], true, true}).evaluation.blocked,
                      "Curve renderer fixture failed");
            RoadRenderer curveRenderer;
            curveRenderer.Initialize(&device, textures, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
            drawnInstances = 0; draws = 0; shapeInstances = {};
            curveRenderer.Prepare(curve); curveRenderer.Draw(&pass);
            Check(drawnInstances == 7 && draws == 3 && shapeInstances[static_cast<std::size_t>(RoadShape::DiagonalCurve)] == 1,
                  "Diagonal curve was not rendered with its dedicated mesh");
            bool found = false;
            for (unsigned i = 0; i < drawnInstances; ++i)
            {
                float values[8]; std::memcpy(values, boundInstances->bytes.data() + i * 32, sizeof(values));
                if (values[0] == 7.5f && values[2] == 7.5f)
                {
                    found = true;
                    Check(values[3] == turn + .5f && values[6] == 1 && values[7] == 0,
                          "Authored curve was stretched or given the wrong rotation/UV mode");
                }
            }
            Check(found, "Curve missing from instance buffer");
        }
        World empty(settings);
        renderer.Prepare(empty);
        drawnInstances = 0;
        renderer.Draw(&pass);
        Check(drawnInstances == 0 && buffers == MeshBuffers, "Switching worlds retained stale road instances");
        renderer.Release(); renderer.Release();
        CheckReleased();
        std::cout << "PASS: sixteen shared mesh batches, diagonal curves and all mixed junction rotations, diagonal junction upgrades, instance layout, upload cache, failure recovery and cleanup\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
