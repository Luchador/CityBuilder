#pragma once

#include <SDL3/SDL.h>

// Shared Direct3D shader compilation and immutable buffer upload helpers.
SDL_GPUShader* CompileShader(SDL_GPUDevice* device, const char* source, SDL_GPUShaderStage stage,
                             Uint32 samplers = 0, const char* preamble = "", Uint32 fragmentUniforms = 0);
SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* device, const void* data, Uint32 size, SDL_GPUBufferUsageFlags usage);
