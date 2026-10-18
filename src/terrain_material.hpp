#pragma once

#include "texture.hpp"

// One terrain tile is one world unit. Smaller repeats show smaller grass.
struct TerrainMaterialSettings
{
    float tilesPerRepeat = 4.0f;
    float textureBlend = 0.5f;   // 0 = elevation colors; 1 = base-color texture.
    float normalStrength = 0.6f; // 0 = mesh normals only; 1 = full normal map.
    bool flipNormalY = false;   // Toggle if the texture's green direction is inverted.
};

// Matches float4 material in the fragment shader (slot 0).
struct alignas(16) TerrainMaterialUniforms
{
    float tilesPerRepeat, textureBlend, normalStrength, normalYSign;
};
static_assert(sizeof(TerrainMaterialUniforms) == 16);
TerrainMaterialUniforms MakeTerrainMaterialUniforms(const TerrainMaterialSettings& settings);

// Owns the sampler; borrows cached textures. Release before TextureManager/device.
class TerrainMaterial
{
public:
    TerrainMaterial() = default;
    ~TerrainMaterial() { Release(); }
    TerrainMaterial(const TerrainMaterial&) = delete;
    TerrainMaterial& operator=(const TerrainMaterial&) = delete;

    void Initialize(SDL_GPUDevice* gpu, TextureManager& textures,
                    std::string_view baseColorPath, std::string_view normalPath);
    void Bind(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
              const TerrainMaterialSettings& settings) const;
    void Release() noexcept;

private:
    SDL_GPUDevice* device = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    const Texture* baseColor = nullptr;
    const Texture* normal = nullptr;
};
