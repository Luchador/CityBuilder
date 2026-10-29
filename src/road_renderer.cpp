#include "road_renderer.hpp"
#include "road_shaders.hpp"
#include "shaders.hpp"
#include "zone_overlay_shaders.hpp"
#include "gpu_utils.hpp"
#include "road_mesh.hpp"
#include "world.hpp"

#include <cmath>
#include <algorithm>
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

    struct RoadInstance
    {
        float x, y, z, quarterTurns;
        float slopeX, slopeZ, lengthScale = 1.0f, repeatAlongRoad = 0.0f;
    };

    static_assert(sizeof(RoadInstance) == 32);

    [[noreturn]] void Fail(const char* message)
    {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

void RoadRenderer::Initialize(SDL_GPUDevice* gpu, TextureManager& textures, SDL_GPUTextureFormat colorFormat)
{
    if (device || !gpu) 
    {
        throw std::logic_error("Invalid road renderer initialization");
    }

    device = gpu;

    try
    {
        const char* base = SDL_GetBasePath();
    
        if (!base) 
        {
            Fail("Find road asset directory");
        }

        const std::string utf8Base(base);
        const std::u8string unicodeBase(utf8Base.begin(), utf8Base.end());
        Uint32 maxMipLevels = 1;
    
        for (std::size_t i = 0; i < meshes.size(); ++i)
        {
            const auto& asset = RoadAssets[i];
            const auto path = std::filesystem::path(unicodeBase) / "assets" / asset.model;
            auto geometry = LoadRoadMesh(path, asset);
            auto& mesh = meshes[i];
    
            if (asset.mixedJunction && (!geometry.hasTexcoords || !std::filesystem::exists(std::filesystem::path(unicodeBase) / "assets" / asset.texture)))
            {
                if (!untexturedAsphalt)
                {
                    constexpr std::array<Uint8, 4> asphalt{70, 72, 74, 255};
                    untexturedAsphalt = textures.CreateRgba(1, 1, asphalt, {TextureContent::Color, false});
                }
                mesh.baseColor = untexturedAsphalt.get();
            }
            else 
            {
                mesh.baseColor = &textures.Load(asset.texture, {TextureContent::Color, true});
            }

            maxMipLevels = std::max(maxMipLevels, mesh.baseColor->MipLevels());
            mesh.vertices = UploadBuffer(device, geometry.vertices.data(), static_cast<Uint32>(geometry.vertices.size() * sizeof(StaticMeshVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
            mesh.indices = UploadBuffer(device, geometry.indices.data(), static_cast<Uint32>(geometry.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
            mesh.indexCount = static_cast<Uint32>(geometry.indices.size());
        }

        SDL_GPUSamplerCreateInfo sampling{};
        sampling.min_filter = sampling.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampling.address_mode_u = sampling.address_mode_v = sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampling.enable_anisotropy = true;
        sampling.max_anisotropy = 8.0f;
        sampling.max_lod = static_cast<float>(maxMipLevels - 1);
        sampler = SDL_CreateGPUSampler(device, &sampling);

        if (!sampler) 
        {
            Fail("Create road sampler");
        }

        vertexShader = CompileShader(device, RoadVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        const std::string fragmentPreamble = std::string(WorldLightingShaderSource) + "#define ZONE_TEXTURE t1\n#define ZONE_SAMPLER s1\n#define ROAD_TEXTURE t2\n#define ROAD_SAMPLER s2\n#define LOT_TEXTURE t3\n#define LOT_SAMPLER s3\n#define ARROW_TEXTURE t4\n#define ARROW_SAMPLER s4\n#define ZONE_UNIFORMS b0\n" + ZoneOverlayShaderSource;
        fragmentShader = CompileShader(device, RoadFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 5, fragmentPreamble.c_str(), 1);

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
            {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(RoadInstance, x)},
            {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(RoadInstance, slopeX)}
        };
    
        SDL_GPUColorTargetDescription color{};
        color.format = colorFormat;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = buffers;
        info.vertex_input_state.num_vertex_buffers = 2;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 5;
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

        if (!pipeline) 
        {
            Fail("Create road pipeline");
        }
    }
    catch (...)
    {
        Release();
        throw;
    }
}

void RoadRenderer::Prepare(const World& world)
{
    if (!IsInitialized())
    {
        return;
    }

    const auto roads = world.GetRoads();

    if (preparedWorld == &world && preparedRevision == world.GetRoadRevision()) 
    {
        return;
    }

    std::array<std::vector<RoadInstance>, static_cast<std::size_t>(RoadShape::Count)> groups;

    for (const auto& road : roads)
    {
        if (!HasRoadModel(road)) 
        {
            continue;
        }

        const auto visual = GetRoadVisual(road);

        groups[static_cast<std::size_t>(visual.shape)].push_back({static_cast<float>(road.tileX) + 0.5f,
            road.surface.elevation + RoadSurfaceLift, static_cast<float>(road.tileZ) + 0.5f,
            visual.quarterTurns, road.surface.slopeX, road.surface.slopeZ, visual.lengthScale,
            visual.shape == RoadShape::Straight ? 1.0f : 0.0f});
    }

    std::vector<RoadInstance> positions;
    positions.reserve(roads.size());

    for (const auto& group : groups) 
    {
        positions.insert(positions.end(), group.begin(), group.end());
    }

    SDL_GPUBuffer* uploaded = nullptr;

    if (!positions.empty())
    {
        uploaded = UploadBuffer(device, positions.data(), static_cast<Uint32>(positions.size() * sizeof(RoadInstance)), SDL_GPU_BUFFERUSAGE_VERTEX);
    }

    // Keep the previous buffer until the replacement upload succeeds.
    if (instances) 
    {
        SDL_ReleaseGPUBuffer(device, instances);
    }

    instances = uploaded;
    instanceCount = static_cast<Uint32>(positions.size());
    Uint32 first = 0;

    for (std::size_t i = 0; i < meshes.size(); ++i)
    {
        meshes[i].firstInstance = first;
        meshes[i].instanceCount = static_cast<Uint32>(groups[i].size());
        first += meshes[i].instanceCount;
    }

    preparedWorld = &world;
    preparedRevision = world.GetRoadRevision();
}

void RoadRenderer::Draw(SDL_GPURenderPass* pass) const
{
    if (!pipeline || !instanceCount) 
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    for (const auto& mesh : meshes)
    {
        if (!mesh.instanceCount)
        {
            continue;
        }

        const SDL_GPUBufferBinding bindings[] = {{mesh.vertices, 0}, {instances, mesh.firstInstance * static_cast<Uint32>(sizeof(RoadInstance))}};
        const SDL_GPUBufferBinding indexBinding{mesh.indices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, bindings, 2);
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const SDL_GPUTextureSamplerBinding texture{mesh.baseColor->Handle(), sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
        SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, mesh.instanceCount, 0, 0, 0);
    }
}

void RoadRenderer::Release() noexcept
{
    if (instances) 
    {
        SDL_ReleaseGPUBuffer(device, instances);
    }

    for (auto& mesh : meshes)
    {
        if (mesh.indices) 
        {
            SDL_ReleaseGPUBuffer(device, mesh.indices);
        }

        if (mesh.vertices) 
        {
            SDL_ReleaseGPUBuffer(device, mesh.vertices);
        }
        mesh = {};
    }

    untexturedAsphalt.reset();

    if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
    if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
    if (sampler) SDL_ReleaseGPUSampler(device, sampler);

    device = nullptr;
    instances = nullptr;
    pipeline = nullptr;
    fragmentShader = vertexShader = nullptr;
    sampler = nullptr;
    preparedWorld = nullptr;
    instanceCount = 0;
    preparedRevision = 0;
}
