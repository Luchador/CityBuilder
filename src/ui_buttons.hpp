#pragma once

#include "ui_button.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class UiImageRenderer;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;

using UiButtonId = std::uint32_t;

struct UiButtonResult
{
    bool consumed = false;
    std::optional<UiButtonId> clicked;
};

// Last added is drawn last and hit-tested first. IDs are unique within a set.
// Button references stay valid across Add(), until Remove(id) or Clear().
class UiButtons
{
public:
    UiButton& Add(UiButtonId id, UiButtonTextures textures);
    UiButton* Find(UiButtonId id);
    const UiButton* Find(UiButtonId id) const;
    bool Remove(UiButtonId id);
    void Clear();

    bool BlocksPointer(float x, float y) const;
    bool HasCapture() const { return capturing; }
    UiButtonResult PointerDown(float x, float y);
    UiButtonResult PointerUp(float x, float y);
    void PointerMove(float x, float y);
    // Retest after layout/state changes; cancel if a mouse-up event was missed.
    void SyncPointer(float x, float y, bool leftButtonDown);
    void CancelInteraction();

    void Draw(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height) const;

private:
    struct Entry { UiButtonId id; std::unique_ptr<UiButton> button; };
    const Entry* TopButton(float x, float y) const;
    std::vector<Entry> entries;
    bool capturing = false;
    UiButtonId capturedId = 0;
};
