#include "road_tool.hpp"
#include "world.hpp"

#include <cstdlib>

RoadTool::RoadTool(ToolSuccessSound successSound) : Tool(successSound)
{
}

void RoadTool::UpdateHover(const ToolContext& context, const ToolPointer& pointer)
{
    hovered.reset();
    path.reset();
    allowed = false;

    if (pointer.HasValidHit(context.world))
    {
        hovered = pointer.hit->tile;
        const auto start = anchor.value_or(*hovered);

        // Rebuild from the anchor so skipped mouse-motion events cannot leave gaps.
        path = RoadPath::FromDrag(start.x, start.z, hovered->x, hovered->z);
        const auto evaluation = context.world.EvaluateRoads(*path);
        allowed = evaluation.valid && evaluation.blocked == 0;
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

    if (anchor && path)
    {
        result = RoadRequest{*path};
    }

    Cancel();

    return result;
}

void RoadTool::Cancel() noexcept
{
    anchor.reset();
    hovered.reset();
    path.reset();
    allowed = false;
}

void RoadTool::BuildPreview(ToolPreview& preview) const
{
    preview = {};

    if (path)
    {
        preview.tiles = ToolTileRect::FromCorners({path->startX, path->startZ}, {path->endX, path->endZ});
        {
            // A cardinal drag can also upgrade an endpoint to a transition.
            // The shader selects each planned module's actual footprint.
            preview.tiles->minX -= RoadFootprintRadius; preview.tiles->minZ -= RoadFootprintRadius;
            preview.tiles->maxX += RoadFootprintRadius; preview.tiles->maxZ += RoadFootprintRadius;
        }
        preview.placementAllowed = allowed;
        preview.roadPath = path;
    }
}
