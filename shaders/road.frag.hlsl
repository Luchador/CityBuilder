// The PNG is sRGB; match the terrain display-space lighting/output convention.
#include "common/world_lighting.hlsli"
#define ZONE_TEXTURE t1
#define ZONE_SAMPLER s1
#define ROAD_TEXTURE t2
#define ROAD_SAMPLER s2
#define LOT_TEXTURE t3
#define LOT_SAMPLER s3
#define ARROW_TEXTURE t4
#define ARROW_SAMPLER s4
#define ZONE_UNIFORMS b0
#include "common/zone_overlay.hlsli"
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);
struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
    nointerpolation float repeatAlongRoad : TEXCOORD3;
};
float3 LinearToSrgb(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, step(color, 0.0031308));
}
float4 main(Input input) : SV_Target0
{
    // Wrap only the straight's longitudinal UV; keep end/curve/junction atlases
    // clamped. Gradients from unwrapped UVs avoid mip seams at repeats.
    float2 uv = input.uv;
    if (input.repeatAlongRoad > 0.5) uv.y = frac(uv.y);
    float3 color = baseColorTexture.SampleGrad(baseColorSampler, uv, ddx(input.uv), ddy(input.uv)).rgb;
    color = LinearToSrgb(color) * WorldBrightness(input.normal);
    return float4(ApplyZoneOverlay(color, input.worldXZ, false), 1.0);
}
