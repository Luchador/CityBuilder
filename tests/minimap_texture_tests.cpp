// These SDL test doubles exercise ownership and upload data without a GPU.
// They do not test SDL's PNG decoder or Direct3D 12 itself.
#include "texture.hpp"
#include "ui_image.hpp"
#include "ui_minimap.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

struct SDL_GPUDevice {};
struct SDL_GPUTexture
{
    SDL_GPUDevice* device;
    std::vector<Uint8> pixels;
};
struct SDL_GPUTransferBuffer
{
    SDL_GPUDevice* device;
    std::vector<Uint8> bytes;
    bool mapped = false;
};
struct SDL_GPUCommandBuffer { bool copying = false; };
struct SDL_GPUCopyPass { SDL_GPUCommandBuffer* commands; };

namespace
{
    enum class Failure { None, BasePath, Decode, Convert, Lock, Texture, Transfer, Map, Acquire, Copy, Submit };
    Failure failure = Failure::None;
    int liveTextures = 0, liveTransfers = 0, liveCommands = 0, liveSurfaces = 0, locks = 0;
    int loads = 0, uploads = 0;
    int imageWidth = 3;
    std::string lastPath;
    int draws = 0;
    UiRect drawnBounds;
    UiImageTransform drawnTransform;
    // Two rows, each with three RGBA pixels and four bytes of padding.
    constexpr std::array<Uint8, 32> pixels = {
        200, 100, 50, 255, 200, 100, 50, 128, 255, 255, 255, 0, 99, 99, 99, 99,
        10, 20, 30, 255, 255, 255, 255, 255, 100, 200, 250, 64, 99, 99, 99, 99
    };

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    template<class Function>
    std::string ExpectFailure(Function action)
    {
        try { action(); }
        catch (const std::exception& error) { return error.what(); }
        Check(false, "expected an exception");
        return {};
    }

    void CheckNoTemporaryResources()
    {
        Check(liveTransfers == 0 && liveCommands == 0 && liveSurfaces == 0 && locks == 0,
              "no temporary resource leaks");
    }

    SDL_Surface* MakeSurface()
    {
        auto* surface = new SDL_Surface{};
        surface->format = SDL_PIXELFORMAT_RGBA32;
        surface->w = imageWidth;
        surface->h = 2;
        surface->pitch = 16;
        surface->pixels = new Uint8[pixels.size()];
        std::memcpy(surface->pixels, pixels.data(), pixels.size());
        ++liveSurfaces;
        return surface;
    }
}

// Capture the production minimap draw's geometry without a window or GPU.
void UiImageRenderer::Release() {}
void UiImageRenderer::Draw(SDL_GPUCommandBuffer*, SDL_GPURenderPass*, const Texture&,
                           UiRect bounds, float, float, UiImageTint, UiImageTransform transform) const
{
    ++draws;
    drawnBounds = bounds;
    drawnTransform = transform;
}

const char* SDLCALL SDL_GetBasePath()
{
    return failure == Failure::BasePath ? nullptr : "/city-test/";
}
const char* SDLCALL SDL_GetError() { return "simulated SDL failure"; }

SDL_Surface* SDLCALL SDL_LoadPNG(const char* path)
{
    ++loads;
    lastPath = path;
    return failure == Failure::Decode ? nullptr : MakeSurface();
}
SDL_Surface* SDLCALL SDL_ConvertSurface(SDL_Surface*, SDL_PixelFormat format)
{
    Check(format == SDL_PIXELFORMAT_RGBA32, "convert to RGBA bytes");
    return failure == Failure::Convert ? nullptr : MakeSurface();
}
void SDLCALL SDL_DestroySurface(SDL_Surface* surface)
{
    if (!surface) return;
    delete[] static_cast<Uint8*>(surface->pixels);
    delete surface;
    --liveSurfaces;
}
bool SDLCALL SDL_LockSurface(SDL_Surface*)
{
    if (failure == Failure::Lock) return false;
    ++locks;
    return true;
}
void SDLCALL SDL_UnlockSurface(SDL_Surface*) { --locks; }

SDL_GPUTexture* SDLCALL SDL_CreateGPUTexture(SDL_GPUDevice* device, const SDL_GPUTextureCreateInfo* info)
{
    if (failure == Failure::Texture) return nullptr;
    Check(info->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM &&
          info->usage == SDL_GPU_TEXTUREUSAGE_SAMPLER, "sampled RGBA8 texture");
    Check(info->width == 3 && info->height == 2 && info->num_levels == 1, "image dimensions");
    ++liveTextures;
    return new SDL_GPUTexture{device, {}};
}
void SDLCALL SDL_ReleaseGPUTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture)
{
    Check(device == texture->device, "release on owning device");
    delete texture;
    --liveTextures;
}
SDL_GPUTransferBuffer* SDLCALL SDL_CreateGPUTransferBuffer(
    SDL_GPUDevice* device, const SDL_GPUTransferBufferCreateInfo* info)
{
    if (failure == Failure::Transfer) return nullptr;
    ++liveTransfers;
    return new SDL_GPUTransferBuffer{device, std::vector<Uint8>(info->size)};
}
void SDLCALL SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer)
{
    Check(device == transfer->device && !transfer->mapped, "release unmapped staging buffer");
    delete transfer;
    --liveTransfers;
}
void* SDLCALL SDL_MapGPUTransferBuffer(SDL_GPUDevice*, SDL_GPUTransferBuffer* transfer, bool)
{
    if (failure == Failure::Map) return nullptr;
    transfer->mapped = true;
    return transfer->bytes.data();
}
void SDLCALL SDL_UnmapGPUTransferBuffer(SDL_GPUDevice*, SDL_GPUTransferBuffer* transfer)
{
    Check(transfer->mapped, "unmap mapped buffer");
    transfer->mapped = false;
}
SDL_GPUCommandBuffer* SDLCALL SDL_AcquireGPUCommandBuffer(SDL_GPUDevice*)
{
    if (failure == Failure::Acquire) return nullptr;
    ++liveCommands;
    return new SDL_GPUCommandBuffer{};
}
SDL_GPUCopyPass* SDLCALL SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer* commands)
{
    if (failure == Failure::Copy) return nullptr;
    commands->copying = true;
    return new SDL_GPUCopyPass{commands};
}
void SDLCALL SDL_UploadToGPUTexture(SDL_GPUCopyPass* pass, const SDL_GPUTextureTransferInfo* source,
                                   const SDL_GPUTextureRegion* destination, bool)
{
    Check(pass->commands->copying && !source->transfer_buffer->mapped, "upload in copy pass after unmap");
    const Uint32 pitch = source->pixels_per_row * 4;
    Check(pitch % 256 == 0 && source->offset % 512 == 0, "D3D12 upload alignment");
    Check(source->rows_per_layer == destination->h, "upload row count");
    auto& output = destination->texture->pixels;
    for (Uint32 y = 0; y < destination->h; ++y)
    {
        const auto first = source->transfer_buffer->bytes.begin() + source->offset + y * pitch;
        output.insert(output.end(), first, first + destination->w * 4);
    }
    ++uploads;
}
void SDLCALL SDL_EndGPUCopyPass(SDL_GPUCopyPass* pass)
{
    pass->commands->copying = false;
    delete pass;
}
bool SDLCALL SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer* commands)
{
    Check(!commands->copying, "end copy pass before submit");
    delete commands;
    --liveCommands;
    return failure != Failure::Submit;
}
bool SDLCALL SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer* commands)
{
    Check(!commands->copying, "no open copy pass on cancellation");
    delete commands;
    --liveCommands;
    return true;
}

int main()
{
    SDL_GPUDevice device;
    {
        TextureManager textures;
        ExpectFailure([&] { textures.Load("ui/circle.png"); });
        ExpectFailure([&] { textures.Initialize(nullptr); });
        failure = Failure::BasePath;
        ExpectFailure([&] { textures.Initialize(&device); });
        failure = Failure::None;
        textures.Initialize(&device);
        const Texture& circle = textures.Load("ui/circle.png");
        Check(circle.Width() == 3 && circle.Height() == 2, "expose image dimensions");
        Check(lastPath == "/city-test/assets/ui/circle.png", "asset path independent of working directory");
        const std::vector<Uint8> expected = {
            200, 100, 50, 255, 100, 50, 25, 128, 0, 0, 0, 0,
            10, 20, 30, 255, 255, 255, 255, 255, 25, 50, 63, 64
        };
        Check(circle.Handle()->pixels == expected, "correct alpha and source row pitch");
        Check(&circle == &textures.Load("ui/circle.png"), "repeat load reuses texture object");
        Check(&circle == &textures.Load(".\\ui\\circle.png"), "backslash and dot aliases share texture");
        Check(&circle == &textures.Load("ui/unused/../circle.png"), "normalized path reuses texture");
        Check(textures.Size() == 1 && loads == 1 && uploads == 1, "cache hit skips decode and upload");
        ExpectFailure([&] { textures.Initialize(&device); });
        for (const char* path : {"", ".", "ui/", "../outside.png", "ui/../../outside.png", "/abs.png", "C:\\abs.png"})
            ExpectFailure([&] { textures.Load(path); });
        Check(loads == 1, "invalid paths never reach decoder");

        auto* firstHandle = circle.Handle();
        for (int i = 0; i < 100; ++i) textures.Load("buildings/house" + std::to_string(i) + ".png");
        Check(&circle == &textures.Load("ui/circle.png") && circle.Handle() == firstHandle,
              "borrowed references survive cache growth");
        Check(liveTextures == 101 && textures.Size() == 101, "one GPU texture per unique path");
        CheckNoTemporaryResources();
        textures.Release();
        textures.Release();
        Check(liveTextures == 0 && textures.Size() == 0, "release frees every cached texture once");
        ExpectFailure([&] { textures.Load("ui/circle.png"); });
        textures.Initialize(&device);
        textures.Load("ui/circle.png");
    }
    Check(liveTextures == 0, "destructor releases cache");

    const std::vector<Uint8> generatedPixels{
        200, 100, 50, 255, 200, 100, 50, 128, 255, 255, 255, 0,
        10, 20, 30, 255, 255, 255, 255, 255, 100, 200, 250, 64
    };
    {
        TextureManager textures;
        ExpectFailure([&] { textures.CreateRgba(3, 2, generatedPixels); });
        textures.Initialize(&device);
        const auto& png = textures.Load("comparison.png");
        auto image = textures.CreateRgba(3, 2, generatedPixels);
        Check(image->Handle()->pixels == png.Handle()->pixels, "generated upload matches PNG alpha and row handling");
        Check(textures.Size() == 1, "generated image is uncached and owned by the caller");
        CheckNoTemporaryResources();

        UiImageRenderer renderer;
        UiMinimap minimap;
        minimap.DrawMap(renderer, nullptr, nullptr, *image, 1280, 720);
        Check(draws == 0, "empty minimap layout draws nothing");

        for (const auto size : {128.0f, 288.0f, 512.0f})
        {
            for (const auto window : {std::array<float, 2>{800, 480}, {1280, 720}, {1920, 1080}})
            {
                UiSettings settings;
                settings.minimapSize = size;
                minimap.SetLayout(CalculateUiLayout(window[0], window[1], settings));
                minimap.DrawMap(renderer, nullptr, nullptr, *image, window[0], window[1]);
                for (const auto uv : {std::array<float, 2>{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.23f, 0.81f}})
                {
                    const float lx = (uv[0] * 2 - 1) * drawnBounds.w * 0.5f;
                    const float ly = (uv[1] * 2 - 1) * drawnBounds.h * 0.5f;
                    const float sx = drawnBounds.x + drawnBounds.w * 0.5f + drawnTransform.m00 * lx + drawnTransform.m01 * ly;
                    const float sy = drawnBounds.y + drawnBounds.h * 0.5f + drawnTransform.m10 * lx + drawnTransform.m11 * ly;
                    const auto expected = minimap.MapToScreen(uv[0], uv[1]);
                    Check(std::abs(sx - expected.x) < 0.001f && std::abs(sy - expected.y) < 0.001f,
                          "texture landmarks must line up with MapToScreen");
                    float mapX = -1, mapZ = -1;
                    Check(minimap.TryScreenToMap(sx, sy, mapX, mapZ) &&
                          std::abs(mapX - uv[0]) < 0.001f && std::abs(mapZ - uv[1]) < 0.001f,
                          "clicking texture landmarks recovers their world coordinates");
                }
                for (unsigned int turn = 0; turn < 4; ++turn)
                {
                    std::vector<UiVertex> outline;
                    minimap.AppendViewOutline(outline, {24, 240, 40, 1.7f, 256, turn});
                    for (const auto v : outline)
                        Check(minimap.Contains(v.x, v.y), "rotated outline stays inside the textured diamond");
                }
            }
        }

        for (const auto dimensions : {std::array<Uint32, 2>{0, 2}, {4097, 2}, {3, 0}, {3, 4097}, {3, 3}})
            ExpectFailure([&] { textures.CreateRgba(dimensions[0], dimensions[1], generatedPixels); });
        image.reset();
        Check(liveTextures == 1, "caller releases generated texture without invalidating cached PNG");
    }

    for (Failure stage : {Failure::Texture, Failure::Transfer, Failure::Map, Failure::Acquire, Failure::Copy, Failure::Submit})
    {
        TextureManager textures;
        textures.Initialize(&device);
        auto kept = textures.CreateRgba(3, 2, generatedPixels);
        auto* previous = kept->Handle();
        failure = stage;
        ExpectFailure([&] { kept = textures.CreateRgba(3, 2, generatedPixels); });
        Check(kept->Handle() == previous && liveTextures == 1, "failed map rebuild preserves previous texture");
        CheckNoTemporaryResources();
        failure = Failure::None;
        kept = textures.CreateRgba(3, 2, generatedPixels);
        Check(liveTextures == 1, "successful map rebuild releases previous texture");
    }

    // A failed load must preserve existing borrowers, clean up, and permit retry.
    for (Failure stage : {Failure::Decode, Failure::Convert, Failure::Lock, Failure::Texture,
                          Failure::Transfer, Failure::Map, Failure::Acquire, Failure::Copy, Failure::Submit})
    {
        TextureManager textures;
        textures.Initialize(&device);
        const Texture& kept = textures.Load("kept.png");
        failure = stage;
        const std::string error = ExpectFailure([&] { textures.Load("broken.png"); });
        Check(error.find("broken.png") != std::string::npos &&
              error.find("simulated SDL failure") != std::string::npos, "error includes path and reason");
        Check(textures.Size() == 1 && liveTextures == 1, "failure leaves existing cache intact");
        Check(&kept == &textures.Load("kept.png"), "cached texture usable after failed load");
        CheckNoTemporaryResources();
        failure = Failure::None;
        textures.Load("broken.png");
        Check(textures.Size() == 2, "failed load can be retried");
    }
    {
        TextureManager textures;
        textures.Initialize(&device);
        for (int width : {0, 4097})
        {
            imageWidth = width;
            ExpectFailure([&] { textures.Load("invalid-size.png"); });
            CheckNoTemporaryResources();
            Check(liveTextures == 0 && textures.Size() == 0, "invalid dimensions allocate no GPU resources");
        }
    }
    Check(liveTextures == 0, "all test textures released");
    CheckNoTemporaryResources();
    std::cout << "PASS: generated/PNG uploads, alpha, pitch, lifetime/failure recovery, diamond rendering and picking\n";
}
