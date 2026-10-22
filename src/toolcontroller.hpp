#pragma once

#include <memory>

#include "tool.hpp"

class ToolController
{
public:
    // The world must stay at the same address and outlive this controller.
    explicit ToolController(const World& world);
    ~ToolController();
    ToolController(const ToolController&) = delete;
    ToolController& operator=(const ToolController&) = delete;

    // Replacing/clearing a tool cancels the previous gesture first.
    void SetTool(std::unique_ptr<Tool> tool);
    void ClearTool();
    const Tool* GetTool() const { return currentTool.get(); }
    bool HasTool() const { return currentTool != nullptr; }
    ToolSuccessSound GetSuccessSound() const noexcept
    {
        return currentTool ? currentTool->GetSuccessSound() : ToolSuccessSound::None;
    }
    bool IsInteracting() const { return interacting; }

    // The application will route world input here after UI/camera arbitration.
    // Begin only on a press originating in the world, never on a flyout release.
    bool Begin(const ToolPointer& pointer);
    ToolResult Update(const ToolPointer& pointer, float elapsedSeconds);
    ToolResult End(const ToolPointer& pointer);
    // Route focus loss, modal windows, and input capture cancellation here.
    // End/Cancel must still be delivered if the cursor moves over UI mid-drag.
    void Cancel() noexcept;
    ToolPreview BuildPreview() const;

private:
    ToolPointer CheckedPointer(const ToolPointer& pointer) const;

    ToolContext context;
    std::unique_ptr<Tool> currentTool;
    bool interacting = false;
};
