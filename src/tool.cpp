#include "tool.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

Tool::~Tool() = default;

ToolTileRect ToolTileRect::FromCorners(ToolTile first, ToolTile second)
{
    return {std::min(first.x, second.x), std::min(first.z, second.z),
            std::max(first.x, second.x), std::max(first.z, second.z)};
}

bool ToolPointer::HasValidHit(const World& world) const
{
    return hit && world.ContainsTile(hit->tile.x, hit->tile.z) &&
        std::isfinite(screenX) && std::isfinite(screenY) &&
        std::isfinite(hit->worldX) && std::isfinite(hit->worldY) &&
        std::isfinite(hit->worldZ);
}
