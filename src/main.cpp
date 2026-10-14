#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "terrain.hpp"
#include "shaders.hpp"
#include "ui.hpp"
#include "pan.hpp"
#include "pan_overlay.hpp"
#include "texture.hpp"

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

static SDL_GPUShader* CompileShader(SDL_GPUDevice* device, const char* source, SDL_GPUShaderStage stage, Uint32 samplers = 0)
{
    ID3DBlob* rawCode = nullptr;
    ID3DBlob* rawErrors = nullptr;
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const HRESULT result = D3DCompile(source, std::strlen(source), vertex ? "terrain.vert" : "terrain.frag", nullptr, nullptr, "main", vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &rawCode, &rawErrors);

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
    info.num_uniform_buffers = vertex ? 1u : 0u;
    info.num_samplers = samplers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);

    if (!shader) 
    {
        Fail("Create shader");
    }

    return shader;
}

static SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* device, const void* data, Uint32 size, SDL_GPUBufferUsageFlags usage)
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

struct Uniforms 
{
    float centerX, centerZ, halfHeight, aspect;
    float red, green, blue, depthOffset;
};

static_assert(sizeof(Uniforms) == 32);

class Application 
{
public:
    ~Application()
    {
        StopMousePan();
        if (device) 
        {
            SDL_WaitForGPUIdle(device);
            panOverlay.Release();
            textures.Release();
            if (panVertexShader) SDL_ReleaseGPUShader(device, panVertexShader);
            if (panFragmentShader) SDL_ReleaseGPUShader(device, panFragmentShader);
            if (terrainPipeline) SDL_ReleaseGPUGraphicsPipeline(device, terrainPipeline);
            if (gridPipeline) SDL_ReleaseGPUGraphicsPipeline(device, gridPipeline);
            if (uiPipeline) SDL_ReleaseGPUGraphicsPipeline(device, uiPipeline);
            if (uiVertexShader) SDL_ReleaseGPUShader(device, uiVertexShader);
            if (uiVertices) SDL_ReleaseGPUBuffer(device, uiVertices);
            if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
            if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
            if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
            if (indices) SDL_ReleaseGPUBuffer(device, indices);
            if (gridVertices) SDL_ReleaseGPUBuffer(device, gridVertices);
            if (depth) SDL_ReleaseGPUTexture(device, depth);
            if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
    
        if (window)
        {
            SDL_DestroyWindow(window);
        }
    }

    void Initialize()
    {
        window = SDL_CreateWindow("City Builder", 1280, 720, SDL_WINDOW_RESIZABLE);

        if (!window) 
        {   
            Fail("Create window");
        }

        if (!SDL_SetWindowMinimumSize(window, 800, 480)) Fail("Set minimum window size");

        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");

        if (!device) 
        {
            Fail("Create Direct3D 12 device");
        }

        if (!SDL_ClaimWindowForGPUDevice(device, window)) 
        {
            Fail("Claim window");
        }

        claimed = true;
        textures.Initialize(device);

        vertexShader = CompileShader(device, VertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        fragmentShader = CompileShader(device, FragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        terrainPipeline = MakePipeline(false);
        gridPipeline = MakePipeline(true);
        uiVertexShader = CompileShader(device, UiVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        uiPipeline = MakeUiPipeline();
        panVertexShader = CompileShader(device, PanVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        panFragmentShader = CompileShader(device, PanFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        panOverlay.Initialize(device, textures, panVertexShader, panFragmentShader,
                              SDL_GetGPUSwapchainTextureFormat(device, window));

        terrainSettings.seed = SDL_rand_bits(); // Use a fixed number here to replay a landscape.
        SDL_Log("Terrain seed: %u", static_cast<unsigned int>(terrainSettings.seed));
        terrain = GenerateTerrain(terrainSettings);
        const TerrainMesh mesh = BuildTerrain(terrain);

        vertices = UploadBuffer(device, mesh.vertices.data(), static_cast<Uint32>(mesh.vertices.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        indices = UploadBuffer(device, mesh.indices.data(), static_cast<Uint32>(mesh.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
        gridVertices = UploadBuffer(device, mesh.lines.data(), static_cast<Uint32>(mesh.lines.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
        indexCount = static_cast<Uint32>(mesh.indices.size());
        lineVertexCount = static_cast<Uint32>(mesh.lines.size());
    }

    void Run()
    {
        Uint64 previous = SDL_GetTicksNS();
        bool running = true;

        while (running) 
        {
            const Uint64 now = SDL_GetTicksNS();
            const float dt = std::min(static_cast<float>((now - previous) / 1.0e9), 0.05f);

            previous = now;
            SDL_Event event;

            while (SDL_PollEvent(&event)) 
            {
                HandlePanEvent(event);
                if (event.type == SDL_EVENT_QUIT) 
                {
                    running = false;
                }
    
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) 
                {
                    if (event.key.key == SDLK_ESCAPE) 
                    {
                        running = false;
                    }
        
                    if (event.key.key == SDLK_G) 
                    {
                        showGrid = !showGrid;
                    }
    
                    if (event.key.key == SDLK_HOME) 
                    {
                        StopMousePan();
                        centerX = centerZ = MapExtent * 0.5f;
                        halfHeight = 120.0f;
                    }
                }

                if (event.type == SDL_EVENT_MOUSE_WHEEL) 
                {
                    int windowWidth = 0, windowHeight = 0;

                    if (!SDL_GetWindowSize(window, &windowWidth, &windowHeight)) 
                    {
                        continue;
                    }

                    const auto layout = CalculateUiLayout(static_cast<float>(windowWidth), static_cast<float>(windowHeight), uiSettings);

                    if (!layout.IsWorldPoint(event.wheel.mouse_x, event.wheel.mouse_y)) 
                    {
                        continue;
                    }

                    float amount = event.wheel.y;
    
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) 
                    {
                        amount = -amount;
                    }
        
                    amount = std::clamp(amount, -10.0f, 10.0f);
    
                    halfHeight = std::clamp(halfHeight * std::pow(0.85f, amount), 4.0f, 200.0f);
                }
            }

            if (!running) 
            {
                break;
            }

            if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) 
            {
                if (mousePan.Active())
                {
                    float x = 0, y = 0;
                    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
                    if (buttons & SDL_BUTTON_RMASK) mousePan.Move(x, y);
                    else StopMousePan(); // Protect against a missed release event.
                }
                PanCamera(dt);
            }
            else
            {
                StopMousePan();
            }

            Render();
        }
    }

private:
    void StopMousePan()
    {
        if (!mousePan.Active()) return;
        mousePan.End();
        SDL_CaptureMouse(false);
        if (restoreCursor) SDL_ShowCursor();
        restoreCursor = false;
    }

    void HandlePanEvent(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
        {
            StopMousePan();
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                 event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                 event.type == SDL_EVENT_WINDOW_HIDDEN ||
                 event.type == SDL_EVENT_WINDOW_RESIZED ||
                 event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            // Avoid stale screen anchors after a resize or loss of focus.
            StopMousePan();
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 event.button.button == SDL_BUTTON_RIGHT && !mousePan.Active())
        {
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window, &width, &height)) return;
            if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) return;
            const auto layout = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);
            if (!layout.IsWorldPoint(event.button.x, event.button.y)) return;
            mousePan.Begin(event.button.x, event.button.y);
            restoreCursor = SDL_CursorVisible();
            if (restoreCursor) SDL_HideCursor();
            // Receive release/motion even outside the client area. SDL also has
            // automatic capture while buttons are held if explicit capture fails.
            if (!SDL_CaptureMouse(true)) SDL_Log("Mouse capture: %s", SDL_GetError());
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            mousePan.Move(event.motion.x, event.motion.y);
        }
    }

    SDL_GPUGraphicsPipeline* MakeUiPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(UiVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[2]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1].offset = offsetof(UiVertex, r);
        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = uiVertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 2;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = false;
        info.depth_stencil_state.enable_depth_write = false;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        // Rendered in its own pass after the terrain, with no depth attachment.
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!pipeline) Fail("Create UI pipeline");
        return pipeline;
    }

    void EnsureUi()
    {
        int width = 0, height = 0;
    
        if (!SDL_GetWindowSize(window, &width, &height)) Fail("Get UI window size");

        const auto next = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);

        if (uiVertices && next.width == uiLayout.width && next.height == uiLayout.height && next.minimap.w == uiLayout.minimap.w && next.toolbar.w == uiLayout.toolbar.w && next.bottomBar.h == uiLayout.bottomBar.h) 
        {
            return;
        }

        const auto mesh = BuildUiBackground(next);

        SDL_GPUBuffer* replacement = UploadBuffer(device, mesh.data(), static_cast<Uint32>(mesh.size() * sizeof(UiVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);

        if (uiVertices) 
        {
            SDL_ReleaseGPUBuffer(device, uiVertices);
        }
    
        uiVertices = replacement;
        uiVertexCount = static_cast<Uint32>(mesh.size());
        uiLayout = next;
    }

    SDL_GPUGraphicsPipeline* MakePipeline(bool grid)
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = sizeof(Vertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[3]{};

        // Position
        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(Vertex, x);

        // Normal
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(Vertex, nx);

        // Color
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[2].offset = offsetof(Vertex, r);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false; // Fully opaque terrain and lines.

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.primitive_type = grid ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = !grid;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail(grid ? "Create grid pipeline" : "Create terrain pipeline");
        }

        return pipeline;
    }

    bool EnsureDepth(Uint32 width, Uint32 height)
    {
        if (depth && width == depthWidth && height == depthHeight) 
        {
            return true;
        }

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* replacement = SDL_CreateGPUTexture(device, &info);

        if (!replacement) 
        {
            return false;
        }

        if (depth) 
        {
            SDL_ReleaseGPUTexture(device, depth);
        }

        depth = replacement;
        depthWidth = width;
        depthHeight = height;

        return true;
    }

    void PanCamera(float dt)
    {
        if (mousePan.Active())
        {
            const PanMotion motion = mousePan.Motion(panSettings);
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window, &width, &height)) return;
            const auto layout = CalculateUiLayout(static_cast<float>(width), static_cast<float>(height), uiSettings);
            // Keep visual scroll speed stable at different zoom levels.
            const float screenToWorld = 2.0f * halfHeight / std::max(1.0f, layout.world.h);
            MoveCamera(motion.x * dt * screenToWorld, motion.y * dt * screenToWorld);
            return; // The dead zone remains stationary even if keys are held.
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        float horizontal = static_cast<float>(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - static_cast<float>(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
        float vertical = static_cast<float>(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) - static_cast<float>(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]);
        const float length = std::sqrt(horizontal * horizontal + vertical * vertical);

        if (length == 0.0f) 
        {
            return;
        }

        const float speed = halfHeight * dt;
        horizontal *= speed / length;
        vertical *= speed / length;

        MoveCamera(horizontal, -vertical);
    }

    void MoveCamera(float right, float down)
    {
        const GroundPan movement = ScreenToGroundPan(right, down);
        centerX = std::clamp(centerX + movement.x, 0.0f, MapExtent);
        centerZ = std::clamp(centerZ + movement.z, 0.0f, MapExtent);
    }

    void Render()
    {
        // Geometry is uploaded only when window/layout dimensions change.
        // Do this before acquiring the presentation command buffer.
        EnsureUi();

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);

        if (!commands) 
        {
            Fail("Acquire frame commands");
        }

        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0, height = 0;

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &width, &height))
        {
            const std::string error = SDL_GetError();
            SDL_CancelGPUCommandBuffer(commands);
            throw std::runtime_error("Acquire swapchain: " + error);
        }

        if (!swapchain) 
        {
            if (!SDL_SubmitGPUCommandBuffer(commands)) 
            {
                Fail("Submit minimized frame");
            }
            SDL_Delay(16);

            return;
        }

        if (!EnsureDepth(width, height)) 
        {
            const std::string error = SDL_GetError();

            // A buffer that acquired a swapchain image must be submitted, not cancelled.
            SDL_SubmitGPUCommandBuffer(commands);
    
            throw std::runtime_error("Create depth texture: " + error);
        }

        SDL_GPUColorTargetInfo color{};
        color.texture = swapchain;
        color.clear_color = {0.08f, 0.12f, 0.18f, 1.0f};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = depth;
        depthTarget.clear_depth = 1.0f;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.cycle = true;

        const int worldWidth = std::max(1, static_cast<int>(std::lround(uiLayout.world.w * static_cast<float>(width) / uiLayout.width)));
        const int worldHeight = std::max(1, static_cast<int>(std::lround(uiLayout.world.h * static_cast<float>(height) / uiLayout.height)));
    
        Uniforms view{centerX, centerZ, halfHeight, static_cast<float>(worldWidth) / static_cast<float>(worldHeight), 1.0f, 1.0f, 1.0f, 0.0f};
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depthTarget);
        SDL_GPUViewport worldViewport{0, 0, static_cast<float>(worldWidth), static_cast<float>(worldHeight), 0, 1};
        SDL_Rect worldScissor{0, 0, worldWidth, worldHeight};
        SDL_SetGPUViewport(pass, &worldViewport);
        SDL_SetGPUScissor(pass, &worldScissor);
        SDL_BindGPUGraphicsPipeline(pass, terrainPipeline);
        SDL_GPUBufferBinding vertexBinding{vertices, 0};
        SDL_GPUBufferBinding indexBinding{indices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
        SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, 0, 0, 0);

        if (showGrid) 
        {
            SDL_BindGPUGraphicsPipeline(pass, gridPipeline);
            SDL_GPUBufferBinding lineBinding{gridVertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &lineBinding, 1);
            view.red = view.green = view.blue = 0.0f;

            // Bring the terrain grid slightly forward to avoid z-fighting.
            view.depthOffset = -0.00001f;
            SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
            SDL_DrawGPUPrimitives(pass, lineVertexCount, 1, 0, 0);
        }

        SDL_EndGPURenderPass(pass);

        // Preserve the city image and cover the docks with opaque UI geometry.
        color.load_op = SDL_GPU_LOADOP_LOAD;
        pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        SDL_GPUViewport uiViewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        SDL_Rect uiScissor{0, 0, static_cast<int>(width), static_cast<int>(height)};
        SDL_SetGPUViewport(pass, &uiViewport);
        SDL_SetGPUScissor(pass, &uiScissor);
        SDL_BindGPUGraphicsPipeline(pass, uiPipeline);
        SDL_GPUBufferBinding uiBinding{uiVertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &uiBinding, 1);
        const float screen[4] = {uiLayout.width, uiLayout.height, 0, 0};
        SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
        SDL_DrawGPUPrimitives(pass, uiVertexCount, 1, 0, 0);
        panOverlay.Draw(commands, pass, uiLayout.width, uiLayout.height, mousePan, panSettings);
        SDL_EndGPURenderPass(pass);

        if (!SDL_SubmitGPUCommandBuffer(commands)) 
        {
            Fail("Submit frame");
        }
    }

    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    bool claimed = false;
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* terrainPipeline = nullptr;
    SDL_GPUGraphicsPipeline* gridPipeline = nullptr;
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUBuffer* indices = nullptr;
    SDL_GPUBuffer* gridVertices = nullptr;
    SDL_GPUTexture* depth = nullptr;
    TerrainSettings terrainSettings;
    TerrainHeightmap terrain;
    AnchoredPan mousePan;
    PanSettings panSettings;
    TextureManager textures;
    PanOverlay panOverlay;
    bool restoreCursor = false;
    SDL_GPUShader* panVertexShader = nullptr;
    SDL_GPUShader* panFragmentShader = nullptr;
    UiSettings uiSettings;
    UiLayout uiLayout;
    SDL_GPUShader* uiVertexShader = nullptr;
    SDL_GPUGraphicsPipeline* uiPipeline = nullptr;
    SDL_GPUBuffer* uiVertices = nullptr;
    Uint32 uiVertexCount = 0;
    Uint32 depthWidth = 0, depthHeight = 0;
    Uint32 indexCount = 0, lineVertexCount = 0;
    bool showGrid = true;
    float centerX = MapExtent * 0.5f, centerZ = MapExtent * 0.5f;
    float halfHeight = 120.0f;
};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) 
    {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    int exitCode = 0;

    try 
    {
        Application app;
        app.Initialize();
        app.Run();
    } 
    catch (const std::exception& error) 
    {
        SDL_Log("City Builder: %s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "City Builder", error.what(), nullptr);
        exitCode = 1;
    }

    SDL_Quit();

    return exitCode;
}
