#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

// UI images use premultiplied alpha. Color images are sampled as sRGB;
// data images (such as normal maps) preserve their linear channel values.
enum class TextureContent { Ui, Color, Data };

struct TextureOptions
{
    TextureContent content = TextureContent::Ui;
    bool mipmaps = false;
};

// An image uploaded to the GPU. PNGs are owned by TextureManager; generated
// images are owned by the caller of CreateRgba. Renderers borrow either kind.
// The default options preserve the existing premultiplied UI image behavior.
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

    Uint32 MipLevels() const noexcept { return mipLevels; }

private:
    friend class TextureManager;
    explicit Texture(SDL_GPUDevice* gpu) : device(gpu) {}

    SDL_GPUDevice* device = nullptr;
    SDL_GPUTexture* handle = nullptr;
    Uint32 width = 0, height = 0;
    Uint32 mipLevels = 1;
    TextureContent content = TextureContent::Ui;
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
    const Texture& Load(std::string_view assetPath, TextureOptions options);

    // Create an uncached image from tightly packed RGBA8 (straight alpha).
    // Caller owns the result and must destroy it before the GPU device.
    std::unique_ptr<Texture> CreateRgba(Uint32 width, Uint32 height, std::span<const Uint8> pixels,
                                        TextureOptions options = {});
    // Replace a region of an owned, generated image. Preserves all other pixels
    // and its original color/alpha convention. Call before acquiring a frame.
    void UpdateRgba(Texture& texture, Uint32 x, Uint32 y, Uint32 width, Uint32 height,
                    std::span<const Uint8> pixels);
    std::size_t Size() const noexcept 
    { 
        return textures.size(); 
    }

    // Invalidates every borrowed Texture reference/pointer. Safe to repeat.
    void Release() noexcept;

private:
    std::unique_ptr<Texture> LoadTexture(const std::string& path, TextureOptions options);
    std::unique_ptr<Texture> UploadTexture(Uint32 width, Uint32 height, const Uint8* pixels,
                                           std::size_t sourcePitch, const std::string& label,
                                           TextureOptions options = {});

    SDL_GPUDevice* device = nullptr;
    std::string assetRoot;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
};
