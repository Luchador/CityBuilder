#include "texture.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace
{
    [[noreturn]] void FailTexture(const std::string& operation, const std::string& path)
    {
        throw std::runtime_error(operation + " '" + path + "': " + SDL_GetError());
    }

    std::string NormalizeAssetPath(std::string_view assetPath)
    {
        std::string name(assetPath);
        std::replace(name.begin(), name.end(), '\\', '/');

        if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Texture path must be relative to assets/: " + name);
        }

        const std::u8string utf8Name(name.begin(), name.end());
        const auto path = std::filesystem::path(utf8Name).lexically_normal();
    
        if (path == "." || path.filename().empty())
        {
            throw std::invalid_argument("Texture path must name a file: " + name);
        }

        for (const auto& part : path)
        {
            if (part == "..")
            {
                throw std::invalid_argument("Texture path leaves assets/: " + name);
            }
        }

        const auto utf8 = path.generic_u8string();
        return std::string(utf8.begin(), utf8.end());
    }
}

Texture::~Texture()
{
    if (handle) 
    {
        SDL_ReleaseGPUTexture(device, handle);
    }
}

void TextureManager::Initialize(SDL_GPUDevice* gpu)
{
    if (device) 
    {
        throw std::logic_error("TextureManager is already initialized");
    }

    if (!gpu) 
    {
        throw std::invalid_argument("TextureManager requires a GPU device");
    }

    const char* base = SDL_GetBasePath();

    if (!base) 
    {
        FailTexture("Find executable directory for", "assets/");
    }

    assetRoot = std::string(base) + "assets/";
    device = gpu;
}

const Texture& TextureManager::Load(std::string_view assetPath)
{
    return Load(assetPath, {});
}

const Texture& TextureManager::Load(std::string_view assetPath, TextureOptions options)
{
    if (!device) 
    {
        throw std::logic_error("Initialize TextureManager before loading images");
    }

    if (options.content != TextureContent::Ui && options.content != TextureContent::Color &&
        options.content != TextureContent::Data)
    {
        throw std::invalid_argument("Unknown texture content type");
    }
    const std::string name = NormalizeAssetPath(assetPath);
    // One file may be used in different roles without sharing incompatible data.
    const std::string key = std::to_string(static_cast<int>(options.content)) +
                            (options.mipmaps ? ":mip:" : ":base:") + name;
    const auto found = textures.find(key);

    if (found != textures.end()) 
    {
        return *found->second;
    }

    // Own the texture before inserting it so a failed allocation cannot leak it.
    auto texture = LoadTexture(assetRoot + name, options);

    const auto entry = textures.emplace(key, std::move(texture));

    return *entry.first->second;
}

std::unique_ptr<Texture> TextureManager::LoadTexture(const std::string& path, TextureOptions options)
{
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface loaded(SDL_LoadPNG(path.c_str()), SDL_DestroySurface);

    if (!loaded) 
    {
        FailTexture("Load PNG", path);
    }

    // Restrict acceptable texture sizes (0, 4096]
    if (loaded->w <= 0 || loaded->h <= 0 || loaded->w > 4096 || loaded->h > 4096)
    {
        throw std::runtime_error("Image dimensions must be between 1 and 4096: " + path);
    }

    Surface rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);

    if (!rgba) 
    {
        FailTexture("Convert PNG to RGBA", path);
    }

    if (!SDL_LockSurface(rgba.get())) 
    {
        FailTexture("Lock image pixels", path);
    }

    std::unique_ptr<SDL_Surface, decltype(&SDL_UnlockSurface)> unlock(rgba.get(), SDL_UnlockSurface);

    return UploadTexture(static_cast<Uint32>(rgba->w), static_cast<Uint32>(rgba->h),
                         static_cast<const Uint8*>(rgba->pixels), static_cast<std::size_t>(rgba->pitch), path, options);
}

std::unique_ptr<Texture> TextureManager::CreateRgba(Uint32 width, Uint32 height, std::span<const Uint8> pixels,
                                                   TextureOptions options)
{
    if (!device)
    {
        throw std::logic_error("Initialize TextureManager before creating images");
    }
    if (width == 0 || height == 0 || width > 4096 || height > 4096 ||
        pixels.size() != static_cast<std::size_t>(width) * height * 4)
    {
        throw std::invalid_argument("RGBA image requires dimensions in 1..4096 and exactly width * height * 4 bytes");
    }
    return UploadTexture(width, height, pixels.data(), static_cast<std::size_t>(width) * 4, "generated RGBA image", options);
}

void TextureManager::UpdateRgba(Texture& texture, Uint32 x, Uint32 y, Uint32 width, Uint32 height,
                                std::span<const Uint8> pixels)
{
    if (!device || texture.device != device || !width || !height || x >= texture.width || y >= texture.height ||
        width > texture.width - x || height > texture.height - y ||
        pixels.size() != static_cast<std::size_t>(width) * height * 4)
        throw std::invalid_argument("Invalid RGBA texture update");
    const Uint32 pitch = (width * 4 + 255u) & ~255u;
    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* commands = nullptr;
    try
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = pitch * height;
        transfer = SDL_CreateGPUTransferBuffer(device, &info);
        if (!transfer) FailTexture("Create texture update buffer", "generated RGBA image");
        auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer, false));
        if (!mapped) FailTexture("Map texture update", "generated RGBA image");
        std::memset(mapped, 0, info.size);
        for (Uint32 row = 0; row < height; ++row)
            for (Uint32 col = 0; col < width; ++col)
            {
                const auto source = (static_cast<std::size_t>(row) * width + col) * 4;
                const auto destination = static_cast<std::size_t>(row) * pitch + col * 4;
                const Uint32 alpha = pixels[source + 3];
                for (int channel = 0; channel < 3; ++channel)
                    mapped[destination + channel] = texture.content == TextureContent::Ui ?
                        static_cast<Uint8>((pixels[source + channel] * alpha + 127) / 255) : pixels[source + channel];
                mapped[destination + 3] = static_cast<Uint8>(alpha);
            }
        SDL_UnmapGPUTransferBuffer(device, transfer);
        commands = SDL_AcquireGPUCommandBuffer(device);
        if (!commands) FailTexture("Acquire texture update", "generated RGBA image");
        auto* pass = SDL_BeginGPUCopyPass(commands);
        if (!pass) FailTexture("Begin texture update", "generated RGBA image");
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = pitch / 4;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion region{};
        region.texture = texture.handle;
        region.x = x; region.y = y; region.w = width; region.h = height; region.d = 1;
        // Do not cycle a partial update: untouched pixels must be preserved.
        SDL_UploadToGPUTexture(pass, &source, &region, false);
        SDL_EndGPUCopyPass(pass);
        if (texture.mipLevels > 1) SDL_GenerateMipmapsForGPUTexture(commands, texture.handle);
        const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
        commands = nullptr;
        if (!submitted) FailTexture("Submit texture update", "generated RGBA image");
        SDL_ReleaseGPUTransferBuffer(device, transfer);
    }
    catch (...)
    {
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
        throw;
    }
}

std::unique_ptr<Texture> TextureManager::UploadTexture(Uint32 width, Uint32 height, const Uint8* pixels,
                                                       std::size_t sourcePitch, const std::string& path,
                                                       TextureOptions options)
{
    auto texture = std::unique_ptr<Texture>(new Texture(device));

    texture->width = width;
    texture->height = height;
    texture->content = options.content;
    if (options.mipmaps)
    {
        for (Uint32 size = std::max(width, height); size > 1; size /= 2)
            ++texture->mipLevels;
    }

    // D3D12 prefers 256-byte rows and 512-byte offsets (our offset is zero).
    const Uint32 pitch = (width * 4 + 255u) & ~255u;

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = options.content == TextureContent::Color ?
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (texture->mipLevels > 1) textureInfo.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = texture->mipLevels;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture->handle = SDL_CreateGPUTexture(device, &textureInfo);

    if (!texture->handle) 
    {
        FailTexture("Create GPU texture", path);
    }

    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* commands = nullptr;

    try
    {
        SDL_GPUTransferBufferCreateInfo upload{};
    
        upload.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        upload.size = pitch * height;
        transfer = SDL_CreateGPUTransferBuffer(device, &upload);
    
        if (!transfer) 
        {
            FailTexture("Create texture upload buffer", path);
        }

        auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer, false));

        if (!mapped) 
        {
            FailTexture("Map texture upload buffer", path);
        }
    
        std::memset(mapped, 0, upload.size);
    
        for (Uint32 y = 0; y < height; ++y)
        {
            const auto* source = pixels + static_cast<std::size_t>(y) * sourcePitch;
            Uint8* destination = mapped + y * pitch;
    
            for (Uint32 x = 0; x < width; ++x)
            {
                // UI uses premultiplied alpha; material color/data stays straight.
                const Uint32 alpha = source[x * 4 + 3];
    
                for (Uint32 channel = 0; channel < 3; ++channel)
                {
                    destination[x * 4 + channel] = options.content == TextureContent::Ui ?
                        static_cast<Uint8>((source[x * 4 + channel] * alpha + 127) / 255) : source[x * 4 + channel];
                }
        
                destination[x * 4 + 3] = static_cast<Uint8>(alpha);
            }
        }

        SDL_UnmapGPUTransferBuffer(device, transfer);

        commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands) 
        {
            FailTexture("Acquire texture upload commands", path);
        }

        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);

        if (!pass) 
        {
            FailTexture("Begin texture upload pass", path);
        }

        SDL_GPUTextureTransferInfo source{};

        source.transfer_buffer = transfer;
        source.pixels_per_row = pitch / 4;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion destination{};
        destination.texture = texture->handle;
        destination.w = width;
        destination.h = height;
        destination.d = 1;

        SDL_UploadToGPUTexture(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);

        // SDL requires this outside any pass, after the base level upload.
        if (texture->mipLevels > 1) SDL_GenerateMipmapsForGPUTexture(commands, texture->handle);

        const bool submitted = SDL_SubmitGPUCommandBuffer(commands);

        commands = nullptr; // SDL consumes it even if submission fails.

        if (!submitted) 
        {
            FailTexture("Submit texture upload", path);
        }
    
        SDL_ReleaseGPUTransferBuffer(device, transfer);

        return texture;
    }
    catch (...)
    {
        if (commands) 
        {
            SDL_CancelGPUCommandBuffer(commands);
        }
    
        if (transfer) 
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }

        throw; // The texture is released automatically.
    }
}

void TextureManager::Release() noexcept
{
    textures.clear();
    assetRoot.clear();
    device = nullptr;
}
