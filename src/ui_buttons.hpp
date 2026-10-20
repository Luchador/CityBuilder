#pragma once

#include "ui_button.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class UiImageRenderer;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;

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
    const UiButton* ButtonAt(float x, float y) const;
    bool Remove(UiButtonId id);
    void Clear();

    // Filter drawing AND hit testing to one button. nullopt restores the set.
    // Individual visibility/enabled/selected states are preserved.
    void ShowOnly(std::optional<UiButtonId> id);

    // First-level flyouts open on trigger press. Add children last so they draw
    // on top. Their individual visible/enabled/selected settings are preserved.
    void AddFlyout(UiButtonId trigger, std::span<const UiButtonId> children);
    // Includes gaps and the connection to the trigger, preventing click-through.
    void SetFlyoutBounds(UiButtonId trigger, UiRect bounds);
    bool HasOpenFlyout() const;

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
    struct Flyout
    {
        UiButtonId trigger;
        std::vector<UiButtonId> children;
        UiRect bounds{};
    };
    const Flyout* FindFlyout(UiButtonId trigger) const;
    const Flyout* ActiveFlyout() const;
    bool IsInActiveFlyout(UiButtonId id) const;
    const Entry* TopButton(float x, float y) const;
    bool IsShown(const Entry& entry) const;
    std::vector<Entry> entries;
    std::vector<Flyout> flyouts;
    std::optional<UiButtonId> openFlyout;
    std::optional<UiButtonId> onlyShown;
    bool capturing = false;
    std::optional<UiButtonId> capturedId;
    bool flyoutGesture = false;
};
