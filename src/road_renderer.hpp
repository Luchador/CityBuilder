#pragma once

#include "texture.hpp"
#include "road.hpp"

class World;

// Owns shared geometry for each shape; borrows cached base-color textures.
// One instance buffer groups roads into at most five draw calls.
class RoadRenderer
{
public:
    RoadRenderer() = default;
    ~RoadRenderer() { Release(); }
    RoadRenderer(const RoadRenderer&) = delete;
    RoadRenderer& operator=(const RoadRenderer&) = delete;

    bool IsInitialized() const { return pipeline != nullptr; }
    void Initialize(SDL_GPUDevice* gpu, TextureManager& textures, SDL_GPUTextureFormat colorFormat);
    // Call before acquiring the frame command buffer. Uploads only after edits.
    void Prepare(const World& world);
    // Caller supplies the shared world View uniform in vertex slot 0.
    void Draw(SDL_GPURenderPass* pass) const;
    void Release() noexcept;

private:
    SDL_GPUDevice* device = nullptr;
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    struct Mesh
    {
        const Texture* baseColor = nullptr;
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        Uint32 indexCount = 0, firstInstance = 0, instanceCount = 0;
    };
    std::array<Mesh, static_cast<std::size_t>(RoadShape::Count)> meshes{};
    SDL_GPUBuffer* instances = nullptr;
    Uint32 instanceCount = 0;
    const World* preparedWorld = nullptr;
    std::uint64_t preparedRevision = 0;
};
