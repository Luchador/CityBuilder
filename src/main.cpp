#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "camera.hpp"
#include "gpu_utils.hpp"
#include "ui_manager.hpp"
#include "city.hpp"
#include "simulation.hpp"
#include "sound.hpp"
#include "terrain.hpp"
#include "terrain_material.hpp"
#include "terrain_shaders.hpp"
#include "terrain_skirt.hpp"
#include "terrain_skirt_shaders.hpp"
#include "toolcontroller.hpp"
#include "shaders.hpp"
#include "water.hpp"
#include "water_shaders.hpp"
#include "world.hpp"
#include "texture.hpp"

[[noreturn]] static void Fail(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

struct Uniforms 
{
    float centerX, centerZ, halfHeight, aspect;
    float red, green, blue, depthOffset;
    CameraOrientation orientation;
    float mapExtent;
    float padding[3]{};
};

static_assert(offsetof(Uniforms, orientation) == 32);
static_assert(offsetof(Uniforms, mapExtent) == 48);
static_assert(sizeof(Uniforms) == 64);

class Application 
{
public:
    ~Application()
    {
        ui.CancelInteraction();
        StopMousePan();

        if (device) 
        {
            SDL_WaitForGPUIdle(device);
            ui.Release();
            ReleaseTerrainBuffers();
            terrainMaterial.Release();
            textures.Release();
            if (terrainPipeline) SDL_ReleaseGPUGraphicsPipeline(device, terrainPipeline);
            if (terrainVertexShader) SDL_ReleaseGPUShader(device, terrainVertexShader);
            if (terrainFragmentShader) SDL_ReleaseGPUShader(device, terrainFragmentShader);
            if (skirtPipeline) SDL_ReleaseGPUGraphicsPipeline(device, skirtPipeline);
            if (skirtVertexShader) SDL_ReleaseGPUShader(device, skirtVertexShader);
            if (skirtFragmentShader) SDL_ReleaseGPUShader(device, skirtFragmentShader);
            if (waterPipeline) SDL_ReleaseGPUGraphicsPipeline(device, waterPipeline);
            if (waterVertexShader) SDL_ReleaseGPUShader(device, waterVertexShader);
            if (waterFragmentShader) SDL_ReleaseGPUShader(device, waterFragmentShader);
            if (gridPipeline) SDL_ReleaseGPUGraphicsPipeline(device, gridPipeline);
            if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
            if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
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
        window = SDL_CreateWindow("Cities Unlimited", 1920, 1080, SDL_WINDOW_RESIZABLE);

        if (!window) 
        {   
            Fail("Create window");
        }

        if (!SDL_SetWindowMinimumSize(window, 1024, 768)) Fail("Set minimum window size");

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

        const std::string worldPreamble = std::string(WorldViewShaderSource) + WorldLightingShaderSource;
        vertexShader = CompileShader(device, VertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, worldPreamble.c_str());
        fragmentShader = CompileShader(device, FragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        terrainVertexShader = CompileShader(device, TerrainVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        terrainFragmentShader = CompileShader(device, TerrainFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, WorldLightingShaderSource, 1);
        terrainPipeline = MakePipeline(false);
        gridPipeline = MakePipeline(true);
        skirtVertexShader = CompileShader(device, TerrainSkirtVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, worldPreamble.c_str());
        skirtFragmentShader = CompileShader(device, TerrainSkirtFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, "", 1);
        skirtPipeline = MakeSkirtPipeline();
        waterVertexShader = CompileShader(device, WaterVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX, 0, WorldViewShaderSource);
        waterFragmentShader = CompileShader(device, WaterFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        waterPipeline = MakeWaterPipeline();

        UiActions uiActions;

        uiActions.zoomCamera = [this](float steps)
        {
            camera.Zoom(steps);
            if (steps > 0.0f) 
            {
                zoomInSound.Play();
            }
            else 
            {
                zoomOutSound.Play();
            }
        };
    
        uiActions.rotateCamera = [this](bool clockwise) { RotateCamera(clockwise); };
        uiActions.moveCameraToMapPoint = [this](float x, float z)
        {
            camera.SetPosition(x * camera.GetMapExtent(), z * camera.GetMapExtent());
        };

        uiActions.setSimulationSpeed = [this](int speed)
        {
            simulation.SetSimulationSpeed(speed);

            if (speed == 0)
            {
                pauseSound.Play();
            }
            else
            {
                buttonClick.Play();
            }
        };

        uiActions.resumeSimulation = [this]()
        {
            simulation.Resume();
            playSound.Play();
        };

        uiActions.cancelWorldInteraction = [this] { StopMousePan(); };
        uiActions.createCity = [this](const CityCreationOptions& options) { CreateCity(options); };
        ui.Initialize(device, window, textures, std::move(uiActions));

        // Load sound effects.
        zoomInSound.Load("sfx/ui/sfx_zoom_in.wav");
        zoomOutSound.Load("sfx/ui/sfx_zoom_out.wav");
        rotateSound.Load("sfx/ui/sfx_rotate.wav");
        playSound.Load("sfx/ui/sfx_play.wav");
        pauseSound.Load("sfx/ui/sfx_pause.wav");
        buttonClick.Load("sfx/ui/sfx_click.wav");
    }

    void Run()
    {
        Uint64 previous = SDL_GetTicksNS();
        bool running = true;

        while (running) 
        {
            const bool cityHasBeenMade = city.has_value();
            const Uint64 now = SDL_GetTicksNS();

            // Collect elapsed milliseconds for game simulation logic.
            const double elapsedMilliseconds = static_cast<double>(now - previous) / 1'000'000.0;

            previous = now;

            const float dt = std::min(static_cast<float>(elapsedMilliseconds / 1000.0), 0.05f);

            SDL_Event event;

            while (SDL_PollEvent(&event)) 
            {
                if (ui.HandleEvent(event))
                {
                    continue;
                }

                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                    continue;
                }

                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key == SDLK_F11 || (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT))))
                {
                    ToggleFullscreen();
                    continue;
                }
    
                if (!city) 
                {
                    continue;
                }
                HandlePanEvent(event);
    
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

                    if (event.key.key == SDLK_COMMA)
                    {
                        RotateCamera(true);
                    }

                    if (event.key.key == SDLK_PERIOD)
                    {
                        RotateCamera(false);
                    }
    
                    if (event.key.key == SDLK_HOME) 
                    {
                        StopMousePan();
                        camera.SetPosition(camera.GetMapExtent() * 0.5f, camera.GetMapExtent() * 0.5f);
                        camera.ResetZoom();
                    }
                }

                if (event.type == SDL_EVENT_MOUSE_WHEEL) 
                {
                    float amount = event.wheel.y;
    
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) 
                    {
                        amount = -amount;
                    }
        
                    camera.Zoom(amount);
                }
            }

            if (!running) 
            {
                break;
            }

            if (!cityHasBeenMade && city) 
            {
                previous = SDL_GetTicksNS();
            }
    
            if (city)
            {
                if (cityHasBeenMade) 
                {
                    simulation.Update(*city, elapsedMilliseconds);
                }
    
                ui.Update(*city, simulation.GetSimulationSpeed());
            }

            if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) 
            {
                if (camera.IsMousePanning())
                {
                    float x = 0, y = 0;
                    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);

                    if (buttons & SDL_BUTTON_RMASK) 
                    {
                        camera.MoveMousePan(x, y);
                    }
                    else 
                    {
                        StopMousePan(); // Protect against a missed release event.
                    }
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
    Camera camera{0.0f}; // No world bounds until Accept creates a map.
    std::optional<City> city;
    std::optional<World> world;
    std::optional<ToolController> toolController;
    Simulation simulation;

    SoundEffect zoomInSound;
    SoundEffect zoomOutSound;
    SoundEffect rotateSound;
    SoundEffect playSound;
    SoundEffect pauseSound;
    SoundEffect buttonClick;

    // Invoked only by the creation form's Accept action. The optional City and
    // World stay empty until all world resources have been prepared.
    void CreateCity(const CityCreationOptions& options)
    {
        if (city) 
        {
            throw std::logic_error("A city has already been created.");
        }

        City created(options);

        try
        {
            terrainMaterial.Initialize(device, textures, "materials/ground/grass01_basecolor.png", "materials/ground/grass01_normal.png");
            // Terrain
            TerrainSettings terrainSettings;
            terrainSettings.dimensions = MapDimensions(GetMapTileCount(options.mapSize));
            terrainSettings.seed = SDL_rand_bits(); // Use a fixed number here to replay a landscape.
            World createdWorld(terrainSettings);
            const auto& heightmap = createdWorld.GetTerrain();
            const TerrainMesh mesh = BuildTerrain(heightmap);

            vertices = UploadBuffer(device, mesh.vertices.data(), static_cast<Uint32>(mesh.vertices.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
            indices = UploadBuffer(device, mesh.indices.data(), static_cast<Uint32>(mesh.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
            gridIndices = UploadBuffer(device, mesh.lineIndices.data(), static_cast<Uint32>(mesh.lineIndices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
            indexCount = static_cast<Uint32>(mesh.indices.size());
            lineIndexCount = static_cast<Uint32>(mesh.lineIndices.size());

            // Terrain Skirt
            const TerrainSkirtMesh skirt = BuildTerrainSkirt(heightmap, skirtSettings);
            skirtUniforms = MakeTerrainSkirtUniforms(skirtSettings, createdWorld.GetTerrainSettings().seed);
            skirtVertices = UploadBuffer(device, skirt.vertices.data(), static_cast<Uint32>(skirt.vertices.size() * sizeof(TerrainSkirtVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
            skirtIndices = UploadBuffer(device, skirt.indices.data(), static_cast<Uint32>(skirt.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
            skirtIndexCount = static_cast<Uint32>(skirt.indices.size());

            // Water
            const WaterMesh water = BuildWater(heightmap);
            waterIndexCount = static_cast<Uint32>(water.indices.size());

            if (waterIndexCount > 0)
            {
                waterVertices = UploadBuffer(device, water.vertices.data(), static_cast<Uint32>(water.vertices.size() * sizeof(WaterVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);
                waterIndices = UploadBuffer(device, water.indices.data(), static_cast<Uint32>(water.indices.size() * sizeof(std::uint32_t)), SDL_GPU_BUFFERUSAGE_INDEX);
            }

            // Prepare dependent UI before publishing the live game state.
            ui.Update(created, 0);
            ui.RebuildMinimap(mesh);
            world.emplace(std::move(createdWorld));
            city.emplace(std::move(created));
            simulation = Simulation{}; // New cities begin paused, with no accumulated time.
            camera = Camera(world->GetDimensions().Extent());
        }
        catch (...)
        {
            ReleaseTerrainBuffers();
            terrainMaterial.Release();
            world.reset();
            city.reset();
            throw; // UiManager displays the error and keeps Accept available.
        }
    }

    void ReleaseTerrainBuffers()
    {
        for (SDL_GPUBuffer** buffer : {&vertices, &indices, &gridIndices, &skirtVertices,
                                       &skirtIndices, &waterVertices, &waterIndices})
        {
            if (*buffer) 
            {
                SDL_ReleaseGPUBuffer(device, *buffer);
            }
    
            *buffer = nullptr;
        }
        indexCount = lineIndexCount = skirtIndexCount = waterIndexCount = 0;
    }

    void ToggleFullscreen()
    {
        // Cancel interactions whose screen coordinates may change.
        StopMousePan();
        ui.CancelInteraction();

        const bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;

        if (!SDL_SetWindowFullscreen(window, !isFullscreen))
        {
            SDL_Log("Could not change fullscreen mode: %s", SDL_GetError());
        }
    }

    void RotateCamera(bool clockwise)
    {
        // Release capture and restore the cursor before changing direction.
        StopMousePan();

        if (clockwise)
        {
            camera.RotateClockwise();
        }
        else
        {
            camera.RotateCounterClockwise();
        }

        rotateSound.Play();
    }

    void StopMousePan()
    {
        if (!camera.IsMousePanning()) 
        {
            return;
        }

        camera.EndMousePan();

        SDL_CaptureMouse(false);

        if (restoreCursor) 
        {
            SDL_ShowCursor();
        }

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
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT && !camera.IsMousePanning())
        {
            if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ||
                !ui.IsWorldPoint(event.button.x, event.button.y))
            {
                return;
            }

            camera.BeginMousePan(event.button.x, event.button.y);
            // Custom panning graphics replace the cursor in both UI modes.
            restoreCursor = SDL_CursorVisible();
    
            if (restoreCursor)
            { 
                SDL_HideCursor();
            }
    
            // Receive release/motion even outside the client area. SDL also has
            // automatic capture while buttons are held if explicit capture fails.
            if (!SDL_CaptureMouse(true)) 
            {
                SDL_Log("Mouse capture: %s", SDL_GetError());
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
           camera.MoveMousePan(event.motion.x, event.motion.y);
        }
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

        // Terrain is fully opaque while the grid has partially transparent.
        color.blend_state.enable_blend = grid;

        if (grid)
        {
            // Mix the grid color with the terrain beneath it.
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;

            // Preserve the existing framebuffer alpha.
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }

        SDL_GPUGraphicsPipelineCreateInfo info{};
        // The grid keeps the solid-color shaders; ground uses the material pair.
        info.vertex_shader = grid ? vertexShader : terrainVertexShader;
        info.fragment_shader = grid ? fragmentShader : terrainFragmentShader;
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

    SDL_GPUGraphicsPipeline* MakeSkirtPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(TerrainSkirtVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[3]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(TerrainSkirtVertex, x);
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(TerrainSkirtVertex, nx);
        attributes[2].location = 2;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        attributes[2].offset = offsetof(TerrainSkirtVertex, terrainHeight);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = skirtVertexShader;
        info.fragment_shader = skirtFragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail("Create terrain skirt pipeline");
        }

        return pipeline;
    }

    SDL_GPUGraphicsPipeline* MakeWaterPipeline()
    {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.pitch = sizeof(WaterVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[2]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(WaterVertex, x);
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        attributes[1].offset = offsetof(WaterVertex, waterDepth);

        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GetGPUSwapchainTextureFormat(device, window);
        color.blend_state.enable_blend = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = waterVertexShader;
        info.fragment_shader = waterFragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 2;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

        if (!pipeline) 
        {
            Fail("Create water pipeline");
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
        if (!city || ui.BlocksWorldKeyboard()) return;
        const auto& layout = ui.GetLayout();

        const bool* keys = SDL_GetKeyboardState(nullptr);

        const float right = static_cast<float>(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - static_cast<float>(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);

        const float down = static_cast<float>(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) - static_cast<float>(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]);

        camera.UpdatePan(right, down, dt, layout.world.h);
    }

    void Render()
    {
        // UI atlas and static geometry uploads precede the presentation commands.
        ui.PrepareFrame(camera.IsMousePanning());

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

        if (!city)
        {
            SDL_GPUColorTargetInfo background{};
            background.texture = swapchain;
            background.clear_color = {0.08f, 0.12f, 0.18f, 1.0f};
            background.load_op = SDL_GPU_LOADOP_CLEAR;
            background.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &background, 1, nullptr);
    
            if (!pass)
            {
                const std::string error = SDL_GetError();
                SDL_SubmitGPUCommandBuffer(commands);
                throw std::runtime_error("Begin startup background pass: " + error);
            }
    
            SDL_EndGPURenderPass(pass);
            try 
            { 
                ui.Draw(commands, swapchain, width, height, camera); 
            }
            catch (...) 
            { 
                SDL_SubmitGPUCommandBuffer(commands); 
                throw; 
            }
        
            if (!SDL_SubmitGPUCommandBuffer(commands)) 
            {
                Fail("Submit startup frame");
            }
    
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

        const SDL_Rect worldRect = ui.GetWorldViewport(width, height);
        const int worldWidth = worldRect.w;
        const int worldHeight = worldRect.h;

        Uniforms view{camera.GetCenterX(), camera.GetCenterZ(), camera.GetHalfHeight(), static_cast<float>(worldWidth) / static_cast<float>(worldHeight), 1.0f, 1.0f, 1.0f, 0.0f, camera.GetOrientation(), camera.GetMapExtent()};

        // Use exactly the same camera and rounded pixel aspect as the world
        // pass. Copy the small dynamic outline before any render pass begins.
        try
        {
            ui.UploadFrame(commands, camera, camera.GetMapExtent(), width, height);
        }
        catch (...)
        {
            // Once a swapchain texture is acquired, submit rather than cancel.
            SDL_SubmitGPUCommandBuffer(commands);
            throw;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depthTarget);
        SDL_GPUViewport worldViewport{0, 0, static_cast<float>(worldWidth), static_cast<float>(worldHeight), 0, 1};
        SDL_Rect worldScissor{0, 0, worldWidth, worldHeight};
        SDL_SetGPUViewport(pass, &worldViewport);
        SDL_SetGPUScissor(pass, &worldScissor);

        // Opaque outward-facing rock walls. Drawing these before the ground
        // lets the terrain cover the shared top edge naturally.
        SDL_BindGPUGraphicsPipeline(pass, skirtPipeline);
        SDL_GPUBufferBinding skirtVertexBinding{skirtVertices, 0};
        SDL_GPUBufferBinding skirtIndexBinding{skirtIndices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &skirtVertexBinding, 1);
        SDL_BindGPUIndexBuffer(pass, &skirtIndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
        SDL_PushGPUFragmentUniformData(commands, 0, &skirtUniforms, sizeof(skirtUniforms));
        SDL_DrawGPUIndexedPrimitives(pass, skirtIndexCount, 1, 0, 0, 0);

        SDL_BindGPUGraphicsPipeline(pass, terrainPipeline);
        SDL_GPUBufferBinding vertexBinding{vertices, 0};
        SDL_GPUBufferBinding indexBinding{indices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
        terrainMaterial.Bind(commands, pass, terrainMaterialSettings);
        SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, 0, 0, 0);

        // Render the terrain line grid.
        if (showGrid) 
        {
            SDL_BindGPUGraphicsPipeline(pass, gridPipeline);

            // Make grid partially transparent.
            constexpr float GridOpacity = 0.3f;
    
            SDL_SetGPUBlendConstants(pass, {GridOpacity, GridOpacity, GridOpacity, GridOpacity});
            SDL_GPUBufferBinding lineBinding{gridIndices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &lineBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            view.red = view.green = view.blue = 0.0f;

            // Bring the terrain grid slightly forward to avoid z-fighting.
            view.depthOffset = -0.00001f;
            SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
            SDL_DrawGPUIndexedPrimitives(pass, lineIndexCount, 1, 0, 0, 0);
        }

        // Draw opaque water after the grid, covering submerged lines. The grid
        // itself still follows the lake bed; there is no floating grid on water.
        if (waterIndexCount > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, waterPipeline);
            SDL_GPUBufferBinding waterVertexBinding{waterVertices, 0};
            SDL_GPUBufferBinding waterIndexBinding{waterIndices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &waterVertexBinding, 1);
            SDL_BindGPUIndexBuffer(pass, &waterIndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            view.red = WaterColor.r;
            view.green = WaterColor.g;
            view.blue = WaterColor.b;
            view.depthOffset = 0.0f;
            SDL_PushGPUVertexUniformData(commands, 0, &view, sizeof(view));
            SDL_DrawGPUIndexedPrimitives(pass, waterIndexCount, 1, 0, 0, 0);
        }

        SDL_EndGPURenderPass(pass);

        try
        {
            ui.Draw(commands, swapchain, width, height, camera);
        }
        catch (...)
        {
            SDL_SubmitGPUCommandBuffer(commands);
            throw;
        }

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
    SDL_GPUShader* terrainVertexShader = nullptr;
    SDL_GPUShader* terrainFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* gridPipeline = nullptr;
    TerrainSkirtSettings skirtSettings;
    TerrainSkirtUniforms skirtUniforms{};
    SDL_GPUShader* skirtVertexShader = nullptr;
    SDL_GPUShader* skirtFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* skirtPipeline = nullptr;
    SDL_GPUBuffer* skirtVertices = nullptr;
    SDL_GPUBuffer* skirtIndices = nullptr;
    Uint32 skirtIndexCount = 0;
    SDL_GPUShader* waterVertexShader = nullptr;
    SDL_GPUShader* waterFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* waterPipeline = nullptr;
    SDL_GPUBuffer* waterVertices = nullptr;
    SDL_GPUBuffer* waterIndices = nullptr;
    Uint32 waterIndexCount = 0;
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUBuffer* indices = nullptr;
    SDL_GPUBuffer* gridIndices = nullptr;
    SDL_GPUTexture* depth = nullptr;
    TextureManager textures;
    TerrainMaterial terrainMaterial;
    TerrainMaterialSettings terrainMaterialSettings;
    UiManager ui;
    bool restoreCursor = false;
    Uint32 depthWidth = 0, depthHeight = 0;
    Uint32 indexCount = 0, lineIndexCount = 0;
    bool showGrid = true;
};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
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
