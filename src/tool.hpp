#pragma once

#include "zone.hpp"
#include "road.hpp"
#include "tool_feedback.hpp"

#include <optional>
#include <variant>

class World;

struct ToolPreviewColor
{
    float r, g, b;
};

// Shared hover colors for non-zone tools.
inline constexpr ToolPreviewColor ValidToolPreviewColor{0.05f, 0.61f, 0.76f};
inline constexpr ToolPreviewColor InvalidToolPreviewColor{1.0f, 0.12f, 0.10f};

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
    bool shiftHeld = false;

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
    bool useMaximumLotSize = false;

    bool operator==(const ZoneRequest&) const = default;
};

struct RoadRequest
{
    RoadPath path;
};

// An empty result means there is no edit to request. Add other request types
// here as terrain, network, and building tools are implemented.
using ToolResult = std::variant<std::monostate, ZoneRequest, RoadRequest>;

// CPU preview data only. A renderer will turn these bounds into an overlay.
// This describes a selection, not a promise that placement rules permit it.
struct ToolPreview
{
    std::optional<ToolTileRect> tiles;
    // Present for zoning (including ZoneType::None for dezoning). Leave empty
    // for other tools to use the shared valid/invalid preview colors.
    std::optional<ZoneType> zoneType;
    // Non-zone tools supply their own rule check; zoning uses ZoneEvaluation
    // and per-tile eligibility instead. This flag never authorizes an edit.
    bool placementAllowed = false;
    // Road previews select their route/footprint inside tiles' bounding rectangle.
    std::optional<RoadPath> roadPath;
    bool useMaximumLotSize = false;
};

class Tool
{
public:
    virtual ~Tool();

    ToolSuccessSound GetSuccessSound() const noexcept { return successSound; }

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

protected:
    // Tools declare feedback without owning audio resources or playing sounds.
    explicit Tool(ToolSuccessSound sound = ToolSuccessSound::None) noexcept : successSound(sound) {}

private:
    ToolSuccessSound successSound;
};
