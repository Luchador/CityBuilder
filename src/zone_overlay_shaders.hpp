#pragma once

inline constexpr char ZoneOverlayShaderSource[] = R"hlsl(
#ifndef ZONE_TEXTURE
#define ZONE_TEXTURE t2
#define ZONE_SAMPLER s2
#define ROAD_TEXTURE t3
#define ROAD_SAMPLER s3
#define LOT_TEXTURE t4
#define LOT_SAMPLER s4
#define ARROW_TEXTURE t5
#define ARROW_SAMPLER s5
#define ZONE_UNIFORMS b1
#endif
Texture2D<float4> zoneMap : register(ZONE_TEXTURE, space2);
SamplerState zoneSampler : register(ZONE_SAMPLER, space2);
Texture2D<float4> roadMap : register(ROAD_TEXTURE, space2);
SamplerState roadSampler : register(ROAD_SAMPLER, space2);
Texture2D<float4> lotMap : register(LOT_TEXTURE, space2);
SamplerState lotSampler : register(LOT_SAMPLER, space2);
Texture2D<float4> lotArrow : register(ARROW_TEXTURE, space2);
SamplerState arrowSampler : register(ARROW_SAMPLER, space2);
cbuffer ZoneOverlay : register(ZONE_UNIFORMS, space3)
{
    float4 zoneSettings; // map size, opacity, preview (0=off, 1=zone, 2=rectangle, 3=road), affordable
    float4 zoneBounds;   // inclusive min X/Z, max X/Z
    float4 zonePreview;  // RGB, zone type
    float4 roadPath;     // start X/Z, end X/Z
    float4 roadBend;     // bend X/Z, lot preview enabled, padding
};

bool OnRoadSegment(float2 tile, float2 start, float2 end)
{
    if (any(tile < min(start, end)) || any(tile > max(start, end))) return false;
    return start.x == end.x || start.y == end.y || abs(tile.x - start.x) == abs(tile.y - start.y);
}
bool OnRoadPath(float2 tile)
{
    return OnRoadSegment(tile, roadPath.xy, roadBend.xy) || OnRoadSegment(tile, roadBend.xy, roadPath.zw);
}

// CPU RoadFootprintBit uses the same 24-bit offset ordering (center implicit).
uint RoadFootprintBit(int2 offset)
{
    if (all(offset == int2(-1, 0))) return 1u;
    if (all(offset == int2(0, -1))) return 2u;
    if (all(offset == int2(1, 0))) return 4u;
    if (all(offset == int2(0, 1))) return 8u;
    if (all(offset == 0) || any(abs(offset) > 2)) return 0u;
    int index = (offset.y + 2) * 5 + offset.x + 2;
    uint bit = 4u + (uint)index - (uint)(index > 7) - (uint)(index > 11) -
        (uint)(index > 12) - (uint)(index > 13) - (uint)(index > 17);
    return 1u << bit;
}
// A shared tile is red if any proposed module covering it is rejected.
// Only current route anchors are sampled, so earlier previews cannot linger.
bool RoadPreviewFootprint(float2 tile, out bool allowed)
{
    bool covered = false;
    allowed = true;
    [loop] for (int z = -2; z <= 2; ++z) [loop] for (int x = -2; x <= 2; ++x)
    {
        float2 anchor = tile - float2(x, z);
        if (OnRoadPath(anchor))
        {
            float4 eligibility = roadMap.SampleLevel(roadSampler, (anchor + 0.5) / zoneSettings.x, 0);
            uint footprint = (uint)round(eligibility.b * 255.0) |
                ((uint)round(eligibility.r * 255.0) << 8u) | ((uint)round(eligibility.a * 255.0) << 16u);
            if ((x == 0 && z == 0) || (footprint & RoadFootprintBit(int2(x, z))) != 0)
            {
                allowed = allowed && eligibility.g > 0.5;
                covered = true;
            }
        }
    }
    return covered;
}

float3 ApplyLotMarkings(float3 color, float3 zoneColor, float4 lot, float2 local, float2 pixelWidth)
{
    uint edges = (uint)lot.r;
    float2 thickness = max(float2(0.022, 0.022), pixelWidth);
    float border = 0.0;
    if ((edges & 1u) != 0) border = max(border, 1.0 - smoothstep(thickness.x, thickness.x + pixelWidth.x, local.x));
    if ((edges & 2u) != 0) border = max(border, 1.0 - smoothstep(thickness.y, thickness.y + pixelWidth.y, local.y));
    if ((edges & 4u) != 0) border = max(border, 1.0 - smoothstep(thickness.x, thickness.x + pixelWidth.x, 1.0 - local.x));
    if ((edges & 8u) != 0) border = max(border, 1.0 - smoothstep(thickness.y, thickness.y + pixelWidth.y, 1.0 - local.y));
    color = lerp(color, zoneColor * 0.30, border * 0.9);
    if (lot.g != 0)
    {
        float2 p = local - (lot.ba / 8.0 - 1.0);
        // The PNG points down (+V). Rotate it into the lot's world facing.
        float2 uv = lot.g == 1 ? -p : lot.g == 2 ? float2(-p.y, p.x) :
                    lot.g == 3 ? p : float2(p.y, -p.x);
        uv = uv / 0.75 + 0.5;
        if (all(uv >= 0.0) && all(uv <= 1.0))
        {
            float4 arrow = lotArrow.SampleLevel(arrowSampler, uv, 0);
            color = lerp(color, arrow.rgb, arrow.a);
        }
    }
    return color;
}

float3 ApplyZoneOverlay(float3 color, float2 worldXZ, bool showPlacedZones)
{
    float2 tile = clamp(floor(worldXZ), 0.0, zoneSettings.x - 1.0);
    float2 pixelWidth = max(fwidth(worldXZ), 0.00001);
    float4 data = zoneMap.SampleLevel(zoneSampler, (tile + 0.5) / zoneSettings.x, 0);
    uint flags = (uint)round(data.a * 255.0);
    uint type = (flags & 127) / 2;
    bool eligible = (flags & 1) != 0;
    bool inPreview = zoneSettings.z > 0 && all(tile >= zoneBounds.xy) && all(tile <= zoneBounds.zw);
    bool canPreviewLot = showPlacedZones && inPreview && zoneSettings.z == 1 && roadBend.z > 0 &&
        zoneSettings.w > 0 && eligible;
    float4 lot = 0.0;
    if (showPlacedZones && (type != 0 || canPreviewLot))
        lot = round(lotMap.SampleLevel(lotSampler, (tile + 0.5) / zoneSettings.x, 0) * 255.0);
    bool plannedLot = canPreviewLot && (((uint)lot.r & 16u) != 0);
    // A proposed lot replaces the old markings on its tiles. Do not blend the
    // old zone color, boundaries or arrows underneath a new zoning preview.
    if (showPlacedZones && type != 0 && !plannedLot)
    {
        color = lerp(color, data.rgb, zoneSettings.y);
        color = ApplyLotMarkings(color, data.rgb, lot, worldXZ - tile, pixelWidth);
    }
    if (inPreview)
    {
        bool roadAllowed = false;
        if (zoneSettings.z == 3 && !RoadPreviewFootprint(tile, roadAllowed)) return color;
        bool unchanged = type == (uint)zonePreview.w;
        float3 tint = !eligible || (!unchanged && zoneSettings.w == 0) ? float3(1.0, 0.12, 0.10) :
            unchanged && !plannedLot ? float3(0.65, 0.65, 0.65) : zonePreview.rgb;
        if (zoneSettings.z == 2) tint = zonePreview.rgb;
        if (zoneSettings.z == 3) tint = roadAllowed ? zonePreview.rgb : float3(1.0, 0.12, 0.10);
        float2 edge = min(frac(worldXZ), 1.0 - frac(worldXZ));
        color = lerp(color, tint, !plannedLot && min(edge.x, edge.y) < 0.045 ? 0.95 : 0.5);
        if (plannedLot) color = ApplyLotMarkings(color, zonePreview.rgb, lot, worldXZ - tile, pixelWidth);
    }
    return color;
}
)hlsl";
