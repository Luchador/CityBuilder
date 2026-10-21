#include "toolcontroller.hpp"

#include <cmath>
#include <utility>

ToolController::ToolController(const World& world) : context{world}
{
}

ToolController::~ToolController()
{
    Cancel();
}

void ToolController::SetTool(std::unique_ptr<Tool> tool)
{
    Cancel();
    currentTool = std::move(tool);
}

void ToolController::ClearTool()
{
    SetTool(nullptr);
}

ToolPointer ToolController::CheckedPointer(const ToolPointer& pointer) const
{
    ToolPointer checked = pointer;
    if (!checked.HasValidHit(context.world))
    {
        checked.hit.reset();
    }
    return checked;
}

bool ToolController::Begin(const ToolPointer& pointer)
{
    if (!currentTool || interacting)
    {
        return false;
    }

    const ToolPointer checked = CheckedPointer(pointer);
    if (!checked.hit)
    {
        return false;
    }

    interacting = currentTool->Begin(context, checked);
    return interacting;
}

ToolResult ToolController::Update(const ToolPointer& pointer, float elapsedSeconds)
{
    if (!currentTool)
    {
        return {};
    }

    // Continuous tools must never receive negative or non-finite brush time.
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0f)
    {
        elapsedSeconds = 0.0f;
    }

    return currentTool->Update(context, CheckedPointer(pointer), elapsedSeconds);
}

ToolResult ToolController::End(const ToolPointer& pointer)
{
    if (!currentTool || !interacting)
    {
        return {};
    }

    interacting = false;
    const ToolPointer checked = CheckedPointer(pointer);
    if (!checked.hit)
    {
        // Never commit at an old hover position when released off the map/UI.
        currentTool->Cancel();
        return {};
    }

    return currentTool->End(context, checked);
}

void ToolController::Cancel() noexcept
{
    interacting = false;
    if (currentTool)
    {
        currentTool->Cancel();
    }
}

ToolPreview ToolController::BuildPreview() const
{
    ToolPreview preview;
    if (currentTool)
    {
        currentTool->BuildPreview(preview);
    }
    return preview;
}
