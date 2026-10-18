#include "gpu_utils.hpp"

#include <d3dcompiler.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

[[noreturn]] static void Fail(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

struct BlobReleaser
{
    void operator()(ID3DBlob* blob) const
    {
        if (blob)
        {
            blob->Release();
        }
    }
};

SDL_GPUShader* CompileShader(SDL_GPUDevice* device, const char* source, SDL_GPUShaderStage stage, Uint32 samplers, const char* preamble, Uint32 fragmentUniforms)
{
    ID3DBlob* rawCode = nullptr;
    ID3DBlob* rawErrors = nullptr;
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const std::string fullSource = std::string(preamble) + source;
    const HRESULT result = D3DCompile(fullSource.data(), fullSource.size(), vertex ? "city.vert" : "city.frag", nullptr, nullptr, "main", vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &rawCode, &rawErrors);

    std::unique_ptr<ID3DBlob, BlobReleaser> code(rawCode), errors(rawErrors);

    if (FAILED(result))
    {
        throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "D3DCompile failed without a diagnostic.");
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code->GetBufferPointer());
    info.code_size = code->GetBufferSize();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = stage;
    info.num_uniform_buffers = vertex ? 1u : fragmentUniforms;
    info.num_samplers = samplers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);

    if (!shader)
    {
        Fail("Create shader");
    }

    return shader;
}

SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* device, const void* data, Uint32 size, SDL_GPUBufferUsageFlags usage)
{
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);

    if (!buffer)
    {
        Fail("Create mesh buffer");
    }

    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* commands = nullptr;

    try
    {
        SDL_GPUTransferBufferCreateInfo staging{};

        staging.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        staging.size = size;
        transfer = SDL_CreateGPUTransferBuffer(device, &staging);

        if (!transfer)
        {
            Fail("Create upload buffer");
        }

        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);

        if (!mapped)
        {
            Fail("Map upload buffer");
        }

        std::memcpy(mapped, data, size);
        SDL_UnmapGPUTransferBuffer(device, transfer);

        commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands)
        {
            Fail("Acquire upload commands");
        }

        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);
        if (!pass) Fail("Begin mesh upload");
        SDL_GPUTransferBufferLocation source{transfer, 0};
        SDL_GPUBufferRegion destination{buffer, 0, size};
        SDL_UploadToGPUBuffer(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);

        const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
        commands = nullptr;

        if (!submitted)
        {
            Fail("Submit mesh upload");
        }

        SDL_ReleaseGPUTransferBuffer(device, transfer);

        return buffer;
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

        SDL_ReleaseGPUBuffer(device, buffer);

        throw;
    }
}
