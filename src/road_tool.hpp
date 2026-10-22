#pragma once

#include "tool.hpp"

class RoadTool final : public Tool
{
public:
    bool Begin(const ToolContext& context, const ToolPointer& pointer) override;
    ToolResult Update(const ToolContext& context, const ToolPointer& pointer, float elapsedSeconds) override;
    ToolResult End(const ToolContext& context, const ToolPointer& pointer) override;
    void Cancel() noexcept override;
    void BuildPreview(ToolPreview& preview) const override;

private:
    void UpdateHover(const ToolContext& context, const ToolPointer& pointer);
    std::optional<ToolTile> anchor;
    std::optional<ToolTile> hovered;
    bool allowed = false;
};
