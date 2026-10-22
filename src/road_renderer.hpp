#pragma once

#include "texture.hpp"

class World;

// Owns GPU geometry/pipeline/sampler, borrows the cached base-color texture.
// The mesh is uploaded once; per-road positions are an instanced vertex stream.
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
    const Texture* baseColor = nullptr;
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUBuffer* indices = nullptr;
    SDL_GPUBuffer* instances = nullptr;
    Uint32 indexCount = 0, instanceCount = 0;
    const World* preparedWorld = nullptr;
};
