#pragma once

inline constexpr char ZoneOverlayShaderSource[] = R"hlsl(
#ifndef ZONE_TEXTURE
#define ZONE_TEXTURE t2
#define ZONE_SAMPLER s2
#define ZONE_UNIFORMS b1
#endif
Texture2D<float4> zoneMap : register(ZONE_TEXTURE, space2);
SamplerState zoneSampler : register(ZONE_SAMPLER, space2);
cbuffer ZoneOverlay : register(ZONE_UNIFORMS, space3)
{
    float4 zoneSettings; // map size, opacity, preview (0=off, 1=zone, 2=rectangle, 3=road), affordable
    float4 zoneBounds;   // inclusive min X/Z, max X/Z
    float4 zonePreview;  // RGB, zone type
    float4 roadPath;     // start X/Z, end X/Z; zonePreview.w = X-first in road mode
};

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
        if (zoneSettings.z == 3)
        {
            // Bounds limit both legs; these equalities keep the rectangle's
            // interior unselected. Matches RoadPath::ForEachTile on the CPU.
            bool onRoute = zonePreview.w != 0 ? (tile.y == roadPath.y || tile.x == roadPath.z) :
                (tile.x == roadPath.x || tile.y == roadPath.w);
            if (!onRoute) return color;
        }
        bool unchanged = type == (uint)zonePreview.w;
        float3 tint = !eligible || (!unchanged && zoneSettings.w == 0) ? float3(1.0, 0.12, 0.10) :
            unchanged ? float3(0.65, 0.65, 0.65) : zonePreview.rgb;
        if (zoneSettings.z == 2) tint = zonePreview.rgb;
        if (zoneSettings.z == 3) tint = eligible || (flags & 128) != 0 ? zonePreview.rgb : float3(1.0, 0.12, 0.10);
        float2 edge = min(frac(worldXZ), 1.0 - frac(worldXZ));
        color = lerp(color, tint, min(edge.x, edge.y) < 0.045 ? 0.95 : 0.5);
    }
    return color;
}
)hlsl";
