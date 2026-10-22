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

// One CPU plan supplies eligibility and footprint sides for every route.
// A shared tile is red if any proposed module covering it is rejected.
bool RoadPreviewFootprint(float2 tile, out bool allowed)
{
    bool covered = false;
    allowed = true;
    const float2 offsets[5] = {float2(0, 0), float2(-1, 0), float2(0, -1), float2(1, 0), float2(0, 1)};
    const uint sides[5] = {0u, 4u, 8u, 1u, 2u};
    [unroll] for (int i = 0; i < 5; ++i)
    {
        float2 anchor = tile + offsets[i];
        if (OnRoadPath(anchor))
        {
            float4 eligibility = roadMap.SampleLevel(roadSampler, (anchor + 0.5) / zoneSettings.x, 0);
            uint footprint = (uint)round(eligibility.b * 255.0);
            if (i == 0 || (footprint & sides[i]) != 0)
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
