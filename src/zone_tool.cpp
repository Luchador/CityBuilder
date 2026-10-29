#include "zone_tool.hpp"

#include <stdexcept>

ZoneTool::ZoneTool(ZoneType type, ToolSuccessSound successSound) : Tool(successSound), zoneType(type)
{
    switch (type)
    {
        case ZoneType::None:
        case ZoneType::ResidentialLight:
        case ZoneType::ResidentialMedium:
        case ZoneType::ResidentialDense:
        case ZoneType::ResidentialMixedUse:
        case ZoneType::CommercialLight:
        case ZoneType::CommercialMedium:
        case ZoneType::CommercialDense:
        case ZoneType::IndustrialFarm:
        case ZoneType::IndustrialMedium:
        case ZoneType::IndustrialDense:
        case ZoneType::Landfill:
        case ZoneType::Seaport:
            break;
        default:
            throw std::invalid_argument("Unknown zone type");
    }
}

void ZoneTool::UpdateHover(const ToolContext& context, const ToolPointer& pointer)
{
    useMaximumLotSize = pointer.shiftHeld;
    useAlternateLayout = pointer.altHeld;
    hovered.reset();
    if (pointer.HasValidHit(context.world))
    {
        hovered = pointer.hit->tile;
    }
}

bool ZoneTool::Begin(const ToolContext& context, const ToolPointer& pointer)
{
    if (anchor)
    {
        return false;
    }

    UpdateHover(context, pointer);
    anchor = hovered;

    return anchor.has_value();
}

ToolResult ZoneTool::Update(const ToolContext& context, const ToolPointer& pointer, float /*elapsedSeconds*/)
{
    UpdateHover(context, pointer);

    return {}; // Hovering/dragging only changes the preview.
}

ToolResult ZoneTool::End(const ToolContext& context, const ToolPointer& pointer)
{
    // Use the release position even if no motion/update frame preceded it.
    UpdateHover(context, pointer);
    ToolResult result;

    if (anchor && hovered)
    {
        result = ZoneRequest{ToolTileRect::FromCorners(*anchor, *hovered), zoneType, pointer.shiftHeld, pointer.altHeld};
    }

    Cancel();

    return result;
}

void ZoneTool::Cancel() noexcept
{
    anchor.reset();
    hovered.reset();
    useMaximumLotSize = false;
    useAlternateLayout = false;
}

void ZoneTool::BuildPreview(ToolPreview& preview) const
{
    preview = {};

    if (hovered)
    {
        preview.tiles = ToolTileRect::FromCorners(anchor.value_or(*hovered), *hovered);
        preview.zoneType = zoneType;
        preview.useMaximumLotSize = useMaximumLotSize;
        preview.useAlternateLayout = useAlternateLayout;
    }
}
