#pragma once

// Compile with WorldViewShaderSource. Location 3 advances once per road,
// while locations 0..2 read the single shared mesh.
inline constexpr char RoadVertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};
Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1,
            float2 uv : TEXCOORD2, float3 instancePosition : TEXCOORD3)
{
    Output result;
    result.position = ProjectWorldPosition(position + instancePosition);
    result.normal = normal;
    result.uv = uv;
    return result;
}
)hlsl";

// Compile with WorldLightingShaderSource. The PNG is an sRGB color texture;
// match the terrain's existing display-space lighting/output convention.
inline constexpr char RoadFragmentShaderSource[] = R"hlsl(
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);
struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};
float3 LinearToSrgb(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, step(color, 0.0031308));
}
float4 main(Input input) : SV_Target0
{
    float3 color = baseColorTexture.Sample(baseColorSampler, input.uv).rgb;
    return float4(LinearToSrgb(color) * WorldBrightness(input.normal), 1.0);
}
)hlsl";
