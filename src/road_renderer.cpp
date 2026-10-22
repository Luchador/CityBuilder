#include "road_renderer.hpp"
#include "road_shaders.hpp"
#include "shaders.hpp"
#include "gpu_utils.hpp"
#include "static_mesh.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    // A small physical lift prevents the coplanar authored road plane from
    // sharing the terrain surface. It does not alter terrain/picking heights.
    constexpr float RoadSurfaceLift = 0.001f;
    struct RoadInstance { float x, y, z, padding = 0.0f; };
    static_assert(sizeof(RoadInstance) == 16);
    [[noreturn]] void Fail(const char* message)
    {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

void RoadRenderer::Initialize(SDL_GPUDevice* gpu, TextureManager& textures, SDL_GPUTextureFormat colorFormat)
{
    if (device || !gpu) throw std::logic_error("Invalid road renderer initialization");
    device = gpu;
    try
    {
        const char* base = SDL_GetBasePath();
        if (!base) Fail("Find road asset directory");
        const std::string utf8Base(base);
        const std::u8string unicodeBase(utf8Base.begin(), utf8Base.end());
        const auto path = std::filesystem::path(unicodeBase) / "assets" / StraightRoadModelPath;
        const auto mesh = LoadStaticGlb(path, 1.0f / MetersPerTile);
        // No silent normalization/stretching: preserve the authored 16 x 8 m
        // proportions and reject a misplaced pivot or oversized single tile.
        for (const auto& v : mesh.vertices)
            if (std::abs(v.x) > 0.5001f || std::abs(v.z) > 0.5001f)
                throw std::runtime_error("Straight road must fit a 16 x 16 meter tile centered at X/Z = 0");
        baseColor = &textures.Load(StraightRoadTexturePath, {TextureContent::Color, true});
        vertices = UploadBuffer(device, mesh.vertices.data(),
                                static_cast<Uint32>(mesh.vertices.size() * sizeof(StaticMeshVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        indices = UploadBuffer(device, mesh.indices.data(),
                               static_cast<Uint32>(mesh.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
        indexCount = static_cast<Uint32>(mesh.indices.size());

        SDL_GPUSamplerCreateInfo sampling{};
        sampling.min_filter = sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampling.address_mode_u = sampling.address_mode_v = sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampling.enable_anisotropy = true;
        sampling.max_anisotropy = 8.0f;
        sampling.max_lod = static_cast<float>(baseColor->MipLevels() - 1);
        sampler = SDL_CreateGPUSampler(device, &sampling);
        if (!sampler) Fail("Create road sampler");

        vertexShader = CompileShader(device, RoadVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        fragmentShader = CompileShader(device, RoadFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, WorldLightingShaderSource);

        SDL_GPUVertexBufferDescription buffers[2]{};
        buffers[0].slot = 0;
        buffers[0].pitch = sizeof(StaticMeshVertex);
        buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffers[1].slot = 1;
        buffers[1].pitch = sizeof(RoadInstance);
        buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        // SDL reserves instance_step_rate; leave it at zero even for instances.
        const SDL_GPUVertexAttribute attributes[] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(StaticMeshVertex, x)},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(StaticMeshVertex, nx)},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(StaticMeshVertex, u)},
            {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(RoadInstance, x)}
        };
        SDL_GPUColorTargetDescription color{};
        color.format = colorFormat;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = buffers;
        info.vertex_input_state.num_vertex_buffers = 2;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 4;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!pipeline) Fail("Create road pipeline");
    }
    catch (...)
    {
        Release();
        throw;
    }
}

void RoadRenderer::Prepare(const World& world)
{
    if (!IsInitialized()) return;
    const auto roads = world.GetRoads();
    // Roads are append-only for now. Use a world edit revision instead when
    // rotation, demolition or terrain movement can change existing records.
    if (preparedWorld == &world && instanceCount == roads.size()) return;
    std::vector<RoadInstance> positions;
    positions.reserve(roads.size());
    for (const auto& road : roads)
        positions.push_back({static_cast<float>(road.tileX) + 0.5f, road.elevation + RoadSurfaceLift,
                             static_cast<float>(road.tileZ) + 0.5f});
    SDL_GPUBuffer* uploaded = nullptr;
    if (!positions.empty())
        uploaded = UploadBuffer(device, positions.data(), static_cast<Uint32>(positions.size() * sizeof(RoadInstance)), SDL_GPU_BUFFERUSAGE_VERTEX);
    // Keep the previous buffer until the replacement upload succeeds.
    if (instances) SDL_ReleaseGPUBuffer(device, instances);
    instances = uploaded;
    instanceCount = static_cast<Uint32>(positions.size());
    preparedWorld = &world;
}

void RoadRenderer::Draw(SDL_GPURenderPass* pass) const
{
    if (!pipeline || !instanceCount) return;
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    const SDL_GPUBufferBinding bindings[] = {{vertices, 0}, {instances, 0}};
    const SDL_GPUBufferBinding indexBinding{indices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, bindings, 2);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const SDL_GPUTextureSamplerBinding texture{baseColor->Handle(), sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
    SDL_DrawGPUIndexedPrimitives(pass, indexCount, instanceCount, 0, 0, 0);
}

void RoadRenderer::Release() noexcept
{
    if (instances) SDL_ReleaseGPUBuffer(device, instances);
    if (indices) SDL_ReleaseGPUBuffer(device, indices);
    if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
    if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
    if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
    if (sampler) SDL_ReleaseGPUSampler(device, sampler);
    device = nullptr;
    instances = indices = vertices = nullptr;
    pipeline = nullptr;
    fragmentShader = vertexShader = nullptr;
    sampler = nullptr;
    baseColor = nullptr;
    preparedWorld = nullptr;
    indexCount = instanceCount = 0;
}
