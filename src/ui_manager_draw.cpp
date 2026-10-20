#include "ui_manager.hpp"

#include "gpu_utils.hpp"
#include "text_shaders.hpp"
#include "ui_image_shaders.hpp"
#include "ui_shaders.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    [[noreturn]] void Fail(const char* operation)
    {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}

void UiManager::Initialize(SDL_GPUDevice* gpu, SDL_Window* targetWindow, TextureManager& sharedTextures, UiActions callbacks)
{
    if (initialized || device) 
    {
        throw std::logic_error("Release UI before initializing it again");
    }

    if (!gpu || !targetWindow) 
    {
        throw std::invalid_argument("UI requires a GPU device and window");
    }

    device = gpu;
    window = targetWindow;
    textures = &sharedTextures;
    actions = std::move(callbacks);

    try
    {
        const auto format = SDL_GetGPUSwapchainTextureFormat(device, window);
        vertexShader = CompileShader(device, UiVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        fragmentShader = CompileShader(device, UiFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT);
        pipeline = MakePipeline();
        textVertexShader = CompileShader(device, TextVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        textFragmentShader = CompileShader(device, TextFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        textRenderer.Initialize(device, textVertexShader, textFragmentShader, format);
        tooltip.Initialize(device, textRenderer);
        bottomBarText.Initialize(textRenderer);
        imageVertexShader = CompileShader(device, UiImageVertexShaderSource, SDL_GPU_SHADERSTAGE_VERTEX);
        imageFragmentShader = CompileShader(device, UiImageFragmentShaderSource, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        imageRenderer.Initialize(device, imageVertexShader, imageFragmentShader, format);
        panOverlay.Initialize(sharedTextures);
        CreateUiButtons(buttons, sharedTextures);
        windowSkin.Load(sharedTextures);
        SetUiMinimized(settings, buttons, settings.minimized);

        initialized = true;
        RefreshLayout();

        CreateMeetWindow();

        cityCreationPending = true;
        CreateNewCityWindow();
    }
    catch (...)
    {
        Release();
        throw;
    }
}

void UiManager::Release()
{
    CancelInteraction();
    initialized = false;
    buttons.Clear();
    windows.clear(); // Destroy their labels before the shared text renderer.
    tooltip.Release();
    windowRanges.clear();
    windowMesh.clear();
    panOverlay.Release();
    bottomBarText = {};
    textRenderer.Release();
    imageRenderer.Release();
    minimapTexture.reset();

    if (device)
    {
        if (pipeline) 
        {
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }
    
        for (auto* shader : {vertexShader, fragmentShader, textVertexShader, textFragmentShader, imageVertexShader, imageFragmentShader})
        {
            if (shader) 
            {
                SDL_ReleaseGPUShader(device, shader);
            }
        }
    
        if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
        if (windowVertices) SDL_ReleaseGPUBuffer(device, windowVertices);
        if (outlineVertices) SDL_ReleaseGPUBuffer(device, outlineVertices);
        if (outlineTransfer) SDL_ReleaseGPUTransferBuffer(device, outlineTransfer);
    }

    vertexShader = fragmentShader = textVertexShader = textFragmentShader = nullptr;
    imageVertexShader = imageFragmentShader = nullptr;
    pipeline = nullptr;
    vertices = outlineVertices = nullptr;
    windowVertices = nullptr;
    focusedWindow = capturedWindow = nullptr;
    nextWindowId = 1;
    createCityWindowId = 0;
    meetWindowId = 0;
    cityCreationPending = false;
    windowSkin = {};
    outlineTransfer = nullptr;
    vertexCount = outlineVertexCount = 0;
    outlineMesh.clear();
    outlineView = {};
    layout = {};
    minimap = {};
    layoutDirty = geometryDirty = outlineDirty = true;
    pauseFlashStart = 0;
    tickerPaused = false;
    actions = {};
    textures = nullptr;
    window = nullptr;
    device = nullptr;
}

void UiManager::EnsureGeometry()
{
    if (!geometryDirty) 
    {
        return;
    }

    auto mesh = BuildUiBackground(layout);
    minimap.AppendGeometry(mesh);
    SDL_GPUBuffer* replacement = UploadBuffer(device, mesh.data(), static_cast<Uint32>(mesh.size() * sizeof(UiVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);

    if (vertices) 
    {
        SDL_ReleaseGPUBuffer(device, vertices);
    }

    vertices = replacement;
    vertexCount = static_cast<Uint32>(mesh.size());
    geometryDirty = false;
}

void UiManager::RebuildMinimap(const TerrainMesh& terrain, std::span<const MinimapTileOverlay> overlays)
{
    if (!initialized) 
    {
        throw std::logic_error("Initialize UI before rebuilding the minimap");
    }

    // Camera movement and panel resizing do not rebuild the terrain image.
    const auto image = BuildMinimapImage(terrain, overlays);
    minimapTexture = textures->CreateRgba(image.width, image.height, image.rgba);
}

void UiManager::PrepareFrame(bool mousePanning)
{
    RefreshLayout();
    float mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const bool windowOwnsPointer = cityCreationPending || capturedWindow || (!buttons.HasCapture() && WindowAt(mouseX, mouseY));

    if (windowOwnsPointer) 
    {
        buttons.PointerMove(-1, -1);
    }
    else 
    {
        UpdateUiButtonPointer(window, buttons, mousePanning);
    }

    // Poll the pointer first so hover follows resize, focus, and capture changes.
    UpdateTickerHover();
    if (!layout.minimized && !cityCreationPending)
    {
        EnsureGeometry();
    }

    int pixelWidth = 0, pixelHeight = 0;

    if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) 
    {
        Fail("Get text pixel scale");
    }

    if (pixelWidth > 0 && pixelHeight > 0)
    {
        const float scale = std::max(static_cast<float>(pixelWidth) / layout.width, static_cast<float>(pixelHeight) / layout.height);

        PrepareWindows(mousePanning, scale);
        PrepareTooltip(mousePanning, static_cast<Uint32>(pixelWidth), static_cast<Uint32>(pixelHeight));

        if (!layout.minimized || tooltip.IsVisible())
        {
            textRenderer.Prepare(scale);
        }
    }
    else
    {
        tooltip.Hide();
    }
}

void UiManager::PrepareTooltip(bool mousePanning, Uint32 pixelWidth, Uint32 pixelHeight)
{
    float x = 0, y = 0;
    const auto mouse = SDL_GetMouseState(&x, &y);
    const auto flags = SDL_GetWindowFlags(window);
    const UiButton* target = nullptr;
    if (!mousePanning && !mouse && !buttons.HasCapture() && !capturedWindow &&
        (flags & SDL_WINDOW_INPUT_FOCUS) && (flags & SDL_WINDOW_MOUSE_FOCUS))
    {
        if (!layout.minimized)
        {
            if (auto* panel = WindowAt(x, y)) target = panel->HoveredButton();
            else if (!cityCreationPending) target = buttons.ButtonAt(x, y);
        }
        else if (!cityCreationPending)
        {
            // The restore button is still available while the docks are hidden.
            target = buttons.ButtonAt(x, y);
        }
    }
    tooltip.Update(target, SDL_GetTicks());
    tooltip.Prepare(layout.width, layout.height, pixelWidth, pixelHeight);
}

void UiManager::UploadFrame(SDL_GPUCommandBuffer* commands, const Camera& camera, float mapExtent, Uint32 pixelWidth, Uint32 pixelHeight)
{
    if (layout.minimized || cityCreationPending)
    {
        return;
    }

    const auto viewport = GetWorldViewport(pixelWidth, pixelHeight);
    UpdateMinimapOutline(commands, {camera.GetCenterX(), camera.GetCenterZ(), camera.GetHalfHeight(), static_cast<float>(viewport.w) / static_cast<float>(viewport.h), mapExtent, camera.GetQuarterTurns()});
}

SDL_GPUGraphicsPipeline* UiManager::MakePipeline()
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
    info.vertex_shader = vertexShader;
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

    if (!pipeline)
    {
        Fail("Create UI pipeline");
    }

    return pipeline;
}

void UiManager::UpdateMinimapOutline(SDL_GPUCommandBuffer* commands, const MinimapView& view)
{
    if (!outlineDirty && view == outlineView)
    {
        return;
    }

    outlineMesh.clear();
    outlineMesh.reserve(MaxOutlineVertices);
    minimap.AppendViewOutline(outlineMesh, view);

    if (outlineMesh.size() > MaxOutlineVertices)
    {
        throw std::runtime_error("Minimap outline exceeds its vertex buffer capacity.");
    }

    const auto count = static_cast<Uint32>(outlineMesh.size());

    if (count > 0)
    {
        // Each rectangular stroke clipped by a diamond has at most eight
        // corners (six triangles). Keep a little spare capacity and reuse
        // these buffers; cycling protects data still used by earlier frames.
        if (!outlineVertices)
        {
            SDL_GPUBufferCreateInfo info{};
            info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            info.size = MaxOutlineVertices * static_cast<Uint32>(sizeof(UiVertex));
            outlineVertices = SDL_CreateGPUBuffer(device, &info);

            if (!outlineVertices) 
            {
                Fail("Create minimap outline buffer");
            }
        }

        if (!outlineTransfer)
        {
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = MaxOutlineVertices * static_cast<Uint32>(sizeof(UiVertex));
            outlineTransfer = SDL_CreateGPUTransferBuffer(device, &info);
    
            if (!outlineTransfer) 
            {
                Fail("Create minimap outline transfer buffer");
            }
        }

        const Uint32 bytes = count * static_cast<Uint32>(sizeof(UiVertex));
        void* mapped = SDL_MapGPUTransferBuffer(device, outlineTransfer, true);

        if (!mapped)
        {
            Fail("Map minimap outline transfer buffer");
        }

        std::memcpy(mapped, outlineMesh.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, outlineTransfer);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);

        if (!copy)
        {
            Fail("Begin minimap outline upload");
        }

        const SDL_GPUTransferBufferLocation source{outlineTransfer, 0};
        const SDL_GPUBufferRegion destination{outlineVertices, 0, bytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
        SDL_EndGPUCopyPass(copy);
    }

    // Setting count to zero hides an entirely clipped outline immediately.
    outlineVertexCount = count;
    outlineView = view;
    outlineDirty = false;
}

void UiManager::Draw(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, Uint32 width, Uint32 height, const Camera& camera)
{
    SDL_GPUColorTargetInfo color{};
    color.texture = target;
    color.store_op = SDL_GPU_STOREOP_STORE;
    // Preserve the city image and cover the docks with opaque UI geometry.
    color.load_op = SDL_GPU_LOADOP_LOAD;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);

    if (!pass) 
    {
        Fail("Begin UI render pass");
    }

    SDL_GPUViewport uiViewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    SDL_Rect uiScissor{0, 0, static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUViewport(pass, &uiViewport);
    SDL_SetGPUScissor(pass, &uiScissor);

    if (!layout.minimized && !cityCreationPending)
    {
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUBufferBinding uiBinding{vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &uiBinding, 1);
        const float screen[4] = {layout.width, layout.height, 0, 0};
        SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
        SDL_DrawGPUPrimitives(pass, vertexCount, 1, 0, 0);

        if (minimapTexture)
        {
            minimap.DrawMap(imageRenderer, commands, pass, *minimapTexture, layout.width, layout.height);
        }

        if (outlineVertexCount > 0)
        {
            // DrawMap binds the image pipeline and its larger uniform block.
            // Restore the solid-color pipeline before drawing the red outline.
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
            const SDL_GPUBufferBinding outlineBinding{outlineVertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &outlineBinding, 1);
            SDL_DrawGPUPrimitives(pass, outlineVertexCount, 1, 0, 0);
        }

        textRenderer.Draw(commands, pass, layout.width, layout.height, width, height);
    }

    if (!cityCreationPending) buttons.Draw(imageRenderer, commands, pass, layout.width, layout.height);

    if (!layout.minimized) 
    {
        DrawWindows(commands, pass, width, height);
    }

    if (!cityCreationPending)
        panOverlay.Draw(imageRenderer, commands, pass, layout.width, layout.height, camera.GetMousePan(), camera.GetPanSettings());
    tooltip.Draw(commands, pass, pipeline, layout.width, layout.height, width, height);
    SDL_EndGPURenderPass(pass);

}
