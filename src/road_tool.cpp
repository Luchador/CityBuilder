#include "road_tool.hpp"
#include "world.hpp"

void RoadTool::UpdateHover(const ToolContext& context, const ToolPointer& pointer)
{
    hovered.reset();
    allowed = false;

    if (pointer.HasValidHit(context.world))
    {
        hovered = pointer.hit->tile;
        allowed = context.world.CheckRoadPlacement(hovered->x, hovered->z) == RoadPlacementStatus::Allowed;
    }
}

bool RoadTool::Begin(const ToolContext& context, const ToolPointer& pointer)
{
    if (anchor) 
    {
        return false;
    }

    UpdateHover(context, pointer);

    anchor = hovered;
    return anchor.has_value();
}

ToolResult RoadTool::Update(const ToolContext& context, const ToolPointer& pointer, float /*elapsedSeconds*/)
{
    UpdateHover(context, pointer);
    return {};
}

ToolResult RoadTool::End(const ToolContext& context, const ToolPointer& pointer)
{
    UpdateHover(context, pointer);
    ToolResult result;

    if (anchor && hovered == anchor)
    {
        result = RoadRequest{*anchor};
    }

    Cancel();

    return result;
}

void RoadTool::Cancel() noexcept
{
    anchor.reset();
    hovered.reset();
    allowed = false;
}

void RoadTool::BuildPreview(ToolPreview& preview) const
{
    preview = {};

    // Suppress a misleading placement preview during an unsupported drag.
    if (hovered && (!anchor || hovered == anchor))
    {
        preview.tiles = ToolTileRect::FromCorners(*hovered, *hovered);
        preview.placementAllowed = allowed;
    }
}
