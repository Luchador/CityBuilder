#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

// An image uploaded to the GPU. PNGs are owned by TextureManager; generated
// images are owned by the caller of CreateRgba. Renderers borrow either kind.
// Pixels are RGBA8 with premultiplied alpha, matching the pan overlay pipeline.
class Texture
{
public:
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    SDL_GPUTexture* Handle() const noexcept 
    { 
        return handle; 
    }

    Uint32 Width() const noexcept 
    { 
        return width; 
    }

    Uint32 Height() const noexcept 
    { 
        return height; 
    }

private:
    friend class TextureManager;
    explicit Texture(SDL_GPUDevice* gpu) : device(gpu) {}

    SDL_GPUDevice* device = nullptr;
    SDL_GPUTexture* handle = nullptr;
    Uint32 width = 0, height = 0;
};

// Use on the application's render thread, with one manager per GPU device.
// Release borrowers first, then this manager, then the SDL GPU device.
class TextureManager
{
public:
    TextureManager() = default;
    ~TextureManager() 
    { 
        Release(); 
    }

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    void Initialize(SDL_GPUDevice* gpu);

    // UTF-8 path relative to assets/, e.g. "ui/dir_circle.png". PNG only for now.
    // Equivalent separators and dot segments share a cache entry. Match the
    // filename's case. Returned references survive later loads, until Release().
    const Texture& Load(std::string_view assetPath);

    // Create an uncached image from tightly packed RGBA8 (straight alpha).
    // Caller owns the result and must destroy it before the GPU device.
    std::unique_ptr<Texture> CreateRgba(Uint32 width, Uint32 height, std::span<const Uint8> pixels);
    std::size_t Size() const noexcept 
    { 
        return textures.size(); 
    }

    // Invalidates every borrowed Texture reference/pointer. Safe to repeat.
    void Release() noexcept;

private:
    std::unique_ptr<Texture> LoadTexture(const std::string& path);
    std::unique_ptr<Texture> UploadTexture(Uint32 width, Uint32 height, const Uint8* pixels,
                                           std::size_t sourcePitch, const std::string& label);

    SDL_GPUDevice* device = nullptr;
    std::string assetRoot;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
};
