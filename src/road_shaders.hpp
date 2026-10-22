#pragma once

// Compile with WorldViewShaderSource. Location 3 advances once per road,
// while locations 0..2 read one of the five shared meshes.
inline constexpr char RoadVertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};
float2 RotateRoad(float2 value, float turns)
{
    if (turns < 0.5) return value;
    if (turns < 1.5) return float2(-value.y, value.x);
    if (turns < 2.5) return -value;
    return float2(value.y, -value.x);
}
Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1,
            float2 uv : TEXCOORD2, float4 instance : TEXCOORD3)
{
    Output result;
    position.xz = RotateRoad(position.xz, instance.w);
    normal.xz = RotateRoad(normal.xz, instance.w);
    position += instance.xyz;
    result.position = ProjectWorldPosition(position);
    result.normal = normal;
    result.uv = uv;
    result.worldXZ = position.xz;
    return result;
}
)hlsl";

// Compile with WorldLightingShaderSource and ZoneOverlayShaderSource (t1/b0).
// The PNG is an sRGB color texture;
// match the terrain's existing display-space lighting/output convention.
inline constexpr char RoadFragmentShaderSource[] = R"hlsl(
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);
struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};
float3 LinearToSrgb(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, step(color, 0.0031308));
}
float4 main(Input input) : SV_Target0
{
    float3 color = baseColorTexture.Sample(baseColorSampler, input.uv).rgb;
    color = LinearToSrgb(color) * WorldBrightness(input.normal);
    return float4(ApplyZoneOverlay(color, input.worldXZ, false), 1.0);
}
)hlsl";
