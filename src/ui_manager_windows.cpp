#include "ui_manager.hpp"

#include "gpu_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

UiWindow& UiManager::AddWindow(std::string_view title, UiRect bounds)
{
    if (!initialized) 
    {
        throw std::logic_error("Initialize UI before creating a window");
    }

    if (nextWindowId == 0) 
    {
        throw std::overflow_error("Window IDs exhausted");
    }

    auto panel = std::make_unique<UiWindow>(nextWindowId++, title, bounds, textRenderer, windowSkin);
    panel->SetScreenSize(layout.width, layout.height);
    auto* result = panel.get();
    windows.push_back(std::move(panel));
    OpenWindow(result->GetId());

    return *result;
}

UiWindow* UiManager::FindWindow(UiWindowId id)
{
    for (auto& panel : windows) 
    {
        if (panel->GetId() == id) 
        {
            return panel.get();
        }
    }

    return nullptr;
}

void UiManager::OpenWindow(UiWindowId id)
{
    auto* panel = FindWindow(id);

    if (!panel) 
    {
        throw std::out_of_range("Unknown UI window ID");
    }

    if (actions.cancelWorldInteraction) 
    {
        actions.cancelWorldInteraction();
    }

    CancelUiButtonInteraction(buttons);
    CancelWindowInteraction();

    if (settings.minimized) 
    {
        SetMinimized(false);
    }

    panel->Open();
    FocusWindow(panel);
}

UiWindow* UiManager::WindowAt(float x, float y) const
{
    if (auto* required = RequiredWindow()) return required->HitTest(x, y) ? required : nullptr;
    if (settings.minimized) 
    {
        return nullptr;
    }

    for (auto it = windows.rbegin(); it != windows.rend(); ++it)
    {
        if ((*it)->HitTest(x, y)) 
        {
            return it->get();
        }
    }

    return nullptr;
}

bool UiManager::BlocksWorldKeyboard() const
{
    return cityCreationPending || (!settings.minimized && focusedWindow && focusedWindow->BlocksKeyboard());
}

UiWindow* UiManager::RequiredWindow() const
{
    if (!cityCreationPending) return nullptr;
    for (const auto& panel : windows)
        if (panel->GetId() == createCityWindowId) return panel.get();
    return nullptr;
}

void UiManager::FocusWindow(UiWindow* target)
{
    if (auto* required = RequiredWindow())
    {
        target = required;
        target->Open();
    }
    if (focusedWindow == target) 
    {
        return;
    }

    if (focusedWindow) 
    {
        focusedWindow->SetFocused(false);
    }

    focusedWindow = target;

    if (target)
    {
        const auto found = std::find_if(windows.begin(), windows.end(), [target](const auto& panel) { return panel.get() == target; });

        if (found != windows.end()) 
        {
            std::rotate(found, found + 1, windows.end());
        }
    
        target->SetFocused(true);
    }

    SyncTextInput();
}

void UiManager::CancelWindowInteraction()
{
    if (capturedWindow) 
    {
        SDL_CaptureMouse(false);
    }

    capturedWindow = nullptr;

    for (auto& panel : windows)
    {
        panel->CancelInteraction();
        panel->SetFocused(false);
    }

    focusedWindow = nullptr;
    SyncTextInput();
}

void UiManager::SyncTextInput()
{
    const bool desired = window && !settings.minimized && focusedWindow && focusedWindow->WantsTextInput() && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
    const void* owner = desired ? focusedWindow->GetTextInputIdentity() : nullptr;

    if (textInputActive && owner != textInputOwner)
    {
        SDL_StopTextInput(window); // Discard any composition belonging to the old field.
        textInputActive = false;
    }
    textInputOwner = owner;

    if (desired && !textInputActive)
    {
        if (!SDL_StartTextInput(window)) throw std::runtime_error(std::string("Start UI text input: ") + SDL_GetError());
        textInputActive = true;
    }
    else if (!desired && textInputActive)
    {
        SDL_StopTextInput(window);
        textInputActive = false;
    }

    if (desired)
    {
        const auto bounds = focusedWindow->GetTextInputBounds();
        const SDL_Rect area{static_cast<int>(std::lround(bounds.x)), static_cast<int>(std::lround(bounds.y)), static_cast<int>(std::lround(bounds.w)), static_cast<int>(std::lround(bounds.h))};
        SDL_SetTextInputArea(window, &area, focusedWindow->GetTextInputCursorOffset());
    }
}

bool UiManager::HandleWindowEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_HIDDEN ||
        event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        CancelWindowInteraction();
        return false; // Camera capture must see these events too.
    }

    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) 
    {
        return false;
    }

    if (RequiredWindow()) FocusWindow(RequiredWindow());

    if (settings.minimized) 
    {
        return false;
    }

    if (focusedWindow && !focusedWindow->IsOpen()) 
    {
        FocusWindow(nullptr);
    }

    if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F11 || (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)))) 
    {
        return false;
    }

    if (focusedWindow && focusedWindow->HandleKey(event))
    {
        if (focusedWindow && !focusedWindow->IsOpen())
        {
            if (capturedWindow) 
            {
                SDL_CaptureMouse(false);
            }

            capturedWindow = nullptr;
            FocusWindow(nullptr);
        }
    
        SyncTextInput();
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (capturedWindow) 
        {
            return true;
        }

        auto* target = WindowAt(event.button.x, event.button.y);
    
        if (target)
        {
            if (actions.cancelWorldInteraction) 
            {
                actions.cancelWorldInteraction();
            }

            CancelUiButtonInteraction(buttons);
            FocusWindow(target);

            if (event.button.button == SDL_BUTTON_LEFT)
            {
                capturedWindow = target;
                target->PointerDown(event.button.x, event.button.y);
                SDL_CaptureMouse(true);
            }
        
            SyncTextInput();
            return true;
        }
    
        FocusWindow(nullptr);
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && capturedWindow)
    {
        auto* target = capturedWindow;
        capturedWindow = nullptr;
        SDL_CaptureMouse(false);
        target->PointerUp(event.button.x, event.button.y);

        if (focusedWindow && !focusedWindow->IsOpen()) 
        {
            FocusWindow(nullptr);
        }

        SyncTextInput();

        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        if (capturedWindow)
        {
            capturedWindow->PointerMove(event.motion.x, event.motion.y);
            SyncTextInput();
            return true;
        }

        auto* target = buttons.HasCapture() ? nullptr : WindowAt(event.motion.x, event.motion.y);

        for (auto& panel : windows)
        {
            if (panel.get() == target) 
            {
                panel->PointerMove(event.motion.x, event.motion.y);
            }
            else 
            {
                panel->PointerLeave();
            }
        }
    
        if (target) 
        { 
            buttons.PointerMove(-1, -1); 
            return true; 
        }
    }

    if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && !capturedWindow)
    {
        for (auto& panel : windows) panel->PointerLeave();
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        return capturedWindow || WindowAt(event.wheel.mouse_x, event.wheel.mouse_y);
    }

    // In particular, right-button releases still reach the camera.
    return false;
}

void UiManager::PrepareWindows(bool mousePanning, float scale)
{
    const auto flags = SDL_GetWindowFlags(window);

    if (!(flags & SDL_WINDOW_INPUT_FOCUS) || settings.minimized || mousePanning)
    {
        CancelWindowInteraction();
    }
    else if (RequiredWindow())
    {
        FocusWindow(RequiredWindow());
    }

    if (focusedWindow && !focusedWindow->IsOpen()) 
    {
        FocusWindow(nullptr);
    }

    float x = 0, y = 0;
    const auto mouse = SDL_GetMouseState(&x, &y);

    if (capturedWindow && (!(mouse & SDL_BUTTON_LMASK) || !capturedWindow->IsOpen()))
    {
        capturedWindow->CancelInteraction(); // A missed mouse-up never activates a control.
        capturedWindow = nullptr;
        SDL_CaptureMouse(false);
    }

    auto* target = (!mousePanning && (flags & SDL_WINDOW_MOUSE_FOCUS) && !buttons.HasCapture()) ? WindowAt(x, y) : nullptr;

    for (auto& panel : windows)
    {
        if (panel.get() == capturedWindow || (!capturedWindow && panel.get() == target)) 
        {
            panel->PointerMove(x, y);
        }
        else 
        {
            panel->PointerLeave();
        }

        panel->Prepare(!settings.minimized && (!cityCreationPending || panel.get() == RequiredWindow()), scale);
    }

    SyncTextInput();

    std::vector<UiVertex> mesh;
    windowRanges.clear();

    for (auto& panel : windows)
    {
        const auto first = static_cast<Uint32>(mesh.size());
        panel->AppendGeometry(mesh);
        if (mesh.size() != first)
        {
            windowRanges.push_back({panel.get(), first, static_cast<Uint32>(mesh.size()) - first});
        }
    }

    if (mesh.size() == windowMesh.size() && (mesh.empty() || std::memcmp(mesh.data(), windowMesh.data(), mesh.size() * sizeof(UiVertex)) == 0)) 
    {
        return;
    }

    if (mesh.size() > std::numeric_limits<Uint32>::max() / sizeof(UiVertex))
    {
        throw std::length_error("Window geometry exceeds the GPU buffer size limit");
    }

    SDL_GPUBuffer* replacement = mesh.empty() ? nullptr : UploadBuffer(device, mesh.data(), static_cast<Uint32>(mesh.size() * sizeof(UiVertex)), SDL_GPU_BUFFERUSAGE_VERTEX);

    if (windowVertices) 
    {
        SDL_ReleaseGPUBuffer(device, windowVertices);
    }

    windowVertices = replacement;
    windowMesh = std::move(mesh);
}

void UiManager::DrawWindows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, Uint32 width, Uint32 height)
{
    for (const auto& range : windowRanges)
    {
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        const SDL_GPUBufferBinding binding{windowVertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        const float screen[4]{layout.width, layout.height, 0, 0};
        SDL_PushGPUVertexUniformData(commands, 0, screen, sizeof(screen));
        SDL_DrawGPUPrimitives(pass, range.count, 1, range.first, 0);
        range.window->DrawImages(imageRenderer, commands, pass, layout.width, layout.height, width, height);
        textRenderer.Draw(commands, pass, layout.width, layout.height, width, height, range.window->GetId());
    }
}
