#pragma once

#include "tool.hpp"

// One tool handles all zone types. ZoneType::None is the dezone operation.
class ZoneTool final : public Tool
{
public:
    explicit ZoneTool(ZoneType type);

    ZoneType GetZoneType() const { return zoneType; }
    bool Begin(const ToolContext& context, const ToolPointer& pointer) override;
    ToolResult Update(const ToolContext& context, const ToolPointer& pointer,
                      float elapsedSeconds) override;
    ToolResult End(const ToolContext& context, const ToolPointer& pointer) override;
    void Cancel() noexcept override;
    void BuildPreview(ToolPreview& preview) const override;

private:
    void UpdateHover(const ToolContext& context, const ToolPointer& pointer);

    ZoneType zoneType;
    std::optional<ToolTile> anchor;
    std::optional<ToolTile> hovered;
};
