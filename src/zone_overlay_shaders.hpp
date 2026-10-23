#pragma once

inline constexpr char ZoneOverlayShaderSource[] = R"hlsl(
#ifndef ZONE_TEXTURE
#define ZONE_TEXTURE t2
#define ZONE_SAMPLER s2
#define ROAD_TEXTURE t3
#define ROAD_SAMPLER s3
#define ZONE_UNIFORMS b1
#endif
Texture2D<float4> zoneMap : register(ZONE_TEXTURE, space2);
SamplerState zoneSampler : register(ZONE_SAMPLER, space2);
Texture2D<float4> roadMap : register(ROAD_TEXTURE, space2);
SamplerState roadSampler : register(ROAD_SAMPLER, space2);
cbuffer ZoneOverlay : register(ZONE_UNIFORMS, space3)
{
    float4 zoneSettings; // map size, opacity, preview (0=off, 1=zone, 2=rectangle, 3=road), affordable
    float4 zoneBounds;   // inclusive min X/Z, max X/Z
    float4 zonePreview;  // RGB, zone type
    float4 roadPath;     // start X/Z, end X/Z; zonePreview.w = route kind in road mode
};

bool OnRoadPath(float2 tile)
{
    if (any(tile < min(roadPath.xy, roadPath.zw)) || any(tile > max(roadPath.xy, roadPath.zw))) return false;
    if (zonePreview.w == 2) return abs(tile.x - roadPath.x) == abs(tile.y - roadPath.y);
    return zonePreview.w != 0 ? (tile.y == roadPath.y || tile.x == roadPath.z) :
        (tile.x == roadPath.x || tile.y == roadPath.w);
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

float3 ApplyZoneOverlay(float3 color, float2 worldXZ, bool showPlacedZones)
{
    float2 tile = clamp(floor(worldXZ), 0.0, zoneSettings.x - 1.0);
    float4 data = zoneMap.SampleLevel(zoneSampler, (tile + 0.5) / zoneSettings.x, 0);
    uint flags = (uint)round(data.a * 255.0);
    uint type = (flags & 127) / 2;
    bool eligible = (flags & 1) != 0;
    if (showPlacedZones && type != 0) color = lerp(color, data.rgb, zoneSettings.y);
    if (zoneSettings.z > 0 && all(tile >= zoneBounds.xy) && all(tile <= zoneBounds.zw))
    {
        bool roadAllowed = false;
        if (zoneSettings.z == 3 && !RoadPreviewFootprint(tile, roadAllowed)) return color;
        bool unchanged = type == (uint)zonePreview.w;
        float3 tint = !eligible || (!unchanged && zoneSettings.w == 0) ? float3(1.0, 0.12, 0.10) :
            unchanged ? float3(0.65, 0.65, 0.65) : zonePreview.rgb;
        if (zoneSettings.z == 2) tint = zonePreview.rgb;
        if (zoneSettings.z == 3) tint = roadAllowed ? zonePreview.rgb : float3(1.0, 0.12, 0.10);
        float2 edge = min(frac(worldXZ), 1.0 - frac(worldXZ));
        color = lerp(color, tint, min(edge.x, edge.y) < 0.045 ? 0.95 : 0.5);
    }
    return color;
}
)hlsl";
