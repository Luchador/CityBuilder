#pragma once

#include "zone.hpp"

#include <optional>
#include <variant>

class World;

struct ToolTile
{
    int x = 0;
    int z = 0;

    bool operator==(const ToolTile&) const = default;
};

// Inclusive tile bounds, independent of the direction of a drag.
struct ToolTileRect
{
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;

    static ToolTileRect FromCorners(ToolTile first, ToolTile second);
    bool operator==(const ToolTileRect&) const = default;
};

// Supplied by terrain picking. World positions are in map units; screen
// positions below are in window coordinates. No picking happens in Tool.
enum class ToolHitSurface { Terrain, Water };

struct ToolHit
{
    ToolTile tile;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    ToolHitSurface surface = ToolHitSurface::Terrain;
};

struct ToolPointer
{
    float screenX = 0.0f;
    float screenY = 0.0f;
    // Empty when off the terrain, over UI, or blocked by camera interaction.
    std::optional<ToolHit> hit;

    bool HasValidHit(const World& world) const;
};

struct ToolContext
{
    // Tools inspect the world and return requests. Applying edits, checking
    // funds/rules, and notifying renderers belong to the future editing layer.
    const World& world;
};

struct ZoneRequest
{
    ToolTileRect tiles;
    ZoneType zoneType = ZoneType::None; // None requests dezoning.
};

// An empty result means there is no edit to request. Add other request types
// here as terrain, network, and building tools are implemented.
using ToolResult = std::variant<std::monostate, ZoneRequest>;

// CPU preview data only. A renderer will turn these bounds into an overlay.
// This describes a selection, not a promise that placement rules permit it.
struct ToolPreview
{
    std::optional<ToolTileRect> tiles;
    std::optional<ZoneType> zoneType;
};

class Tool
{
public:
    virtual ~Tool();

    // Return true to capture a new gesture. Begin requires a valid world hit.
    virtual bool Begin(const ToolContext& context, const ToolPointer& pointer) = 0;
    // Called every frame, including while hovering. Continuous tools can emit
    // requests here. Time is real elapsed seconds, independent of game speed.
    virtual ToolResult Update(const ToolContext& context, const ToolPointer& pointer,
                              float elapsedSeconds) = 0;
    // Finish the gesture, optionally returning its requested edit.
    virtual ToolResult End(const ToolContext& context, const ToolPointer& pointer) = 0;
    // Clear pending input/preview; this does not undo already-applied edits.
    virtual void Cancel() noexcept = 0;
    virtual void BuildPreview(ToolPreview& preview) const = 0;
};
