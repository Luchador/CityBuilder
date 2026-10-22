#include "road_renderer.hpp"
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
struct SDL_GPUBuffer { std::vector<Uint8> bytes; };
namespace
{
    int buffers = 0, shaders = 0, samplers = 0, pipelines = 0, uploads = 0, draws = 0;
    bool failPipeline = false, failUpload = false;
    Uint32 drawnInstances = 0;
    SDL_GPUBuffer* boundInstances = nullptr;
    SDL_GPUBuffer* boundIndices = nullptr;
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
        Check(!buffers && !shaders && !samplers && !pipelines, "Road GPU resource leaked");
    }
}

const char* SDL_GetBasePath() { return basePath.c_str(); }
const char* SDL_GetError() { return "Injected GPU failure"; }
Texture::~Texture() { delete handle; }
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
SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice*, const void* data, Uint32 size, SDL_GPUBufferUsageFlags)
{
    if (failUpload) throw std::runtime_error("Injected upload failure");
    const auto* bytes = static_cast<const Uint8*>(data);
    auto* buffer = new SDL_GPUBuffer{{bytes, bytes + size}};
    ++buffers; ++uploads;
    return buffer;
}
SDL_GPUShader* CompileShader(SDL_GPUDevice*, const char*, SDL_GPUShaderStage stage, Uint32 samplerCount,
                             const char* preamble, Uint32 uniformCount)
{
    Check(preamble && *preamble && uniformCount == (stage == SDL_GPU_SHADERSTAGE_VERTEX ? 0u : 1u) &&
          samplerCount == (stage == SDL_GPU_SHADERSTAGE_VERTEX ? 0u : 2u), "Incorrect road shader resources");
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
    Check(input.num_vertex_buffers == 2 && input.num_vertex_attributes == 4 &&
          input.vertex_buffer_descriptions[0].pitch == 32 && input.vertex_buffer_descriptions[1].pitch == 16 &&
          input.vertex_buffer_descriptions[1].input_rate == SDL_GPU_VERTEXINPUTRATE_INSTANCE &&
          input.vertex_buffer_descriptions[1].instance_step_rate == 0 && input.vertex_attributes[3].buffer_slot == 1 &&
          input.vertex_attributes[3].format == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
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
          instanceOffset + instances * 16 <= boundInstances->bytes.size(), "Incorrect indexed road draw");
    const Uint32 counts[] = {6, 24, 30, 93, 72};
    const auto shape = static_cast<std::size_t>(std::find(std::begin(counts), std::end(counts), indices) - std::begin(counts));
    Check(shape < RoadAssets.size() && boundTexture->path == RoadAssets[shape].texture, "Wrong mesh/texture pair");
    shapeInstances[shape] += instances;
    for (Uint32 i = 0; i < instances; ++i)
    {
        float values[4];
        std::memcpy(values, boundInstances->bytes.data() + instanceOffset + i * 16, sizeof(values));
        Check(values[3] >= 0 && values[3] <= 3 && std::floor(values[3]) == values[3], "Invalid rotation in instance");
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
        Check(draws == 0 && buffers == 10, "Empty world allocated/drew instances");
        Check(world.PlaceRoad(3, 4), "Test road placement failed");
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == 1 && buffers == 11 && boundInstances->bytes.size() == 16 &&
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
        Check(drawnInstances == 1 && buffers == 11, "Failed upload discarded previous render data");
        failUpload = false;
        drawnInstances = 0;
        instanceTurns.clear();
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == 2 && buffers == 11 && uploads == previousUploads + 1 &&
              instanceTurns == std::vector<float>{2, 0}, "Two ends did not face each other using shared instances");

        Check(world.PlaceRoads({4, 3, 4, 5, false}).changed == 2, "Cross fixture failed");
        Check(world.PlaceRoad(5, 4), "Cross fixture failed");
        Check(world.PlaceRoads({8, 8, 11, 9}).changed == 5 && world.PlaceRoad(9, 7), "Mixed shapes fixture failed");
        drawnInstances = 0; draws = 0; shapeInstances = {}; instanceTurns.clear();
        renderer.Prepare(world); renderer.Draw(&pass);
        Check(drawnInstances == world.GetRoads().size() && draws == 5 && buffers == 11,
              "Five shapes were not batched into five draws/one instance buffer");
        for (const auto count : shapeInstances) Check(count > 0, "A required shape was not drawn");
        const auto mixedUploads = uploads;
        renderer.Prepare(world);
        Check(uploads == mixedUploads, "Stable connected roads reuploaded");
        World empty(settings);
        renderer.Prepare(empty);
        drawnInstances = 0;
        renderer.Draw(&pass);
        Check(drawnInstances == 0 && buffers == 10, "Switching worlds retained stale road instances");
        renderer.Release(); renderer.Release();
        CheckReleased();
        std::cout << "PASS: five mesh/texture batches, rotated instances, previews' shader bindings, upload cache, failure recovery and cleanup\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
