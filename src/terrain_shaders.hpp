#pragma once

// Compile with WorldViewShaderSource. UVs come from world position, so Vertex
// needs no new fields and grass stays fixed during camera movement/rotation.
inline constexpr char TerrainVertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1, float3 vertexColor : TEXCOORD2)
{
    Output result;
    result.position = ProjectWorldPosition(position);
    result.color = vertexColor * style.rgb;
    result.normal = normal;
    result.worldXZ = position.xz;
    return result;
}
)hlsl";

// Compile with WorldLightingShaderSource. SDL fragment texture/sampler slots
// use space2; fragment uniform buffers use space3.
inline constexpr char TerrainFragmentShaderSource[] = R"hlsl(
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);
Texture2D<float4> normalTexture : register(t1, space2);
SamplerState normalSampler : register(s1, space2);
cbuffer Material : register(b0, space3)
{
    float4 material; // tiles per repeat, texture blend, normal strength, green sign
};

struct Input {
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

float3 SrgbToLinear(float3 color)
{
    color = saturate(color);
    return lerp(pow((color + 0.055) / 1.055, 2.4), color / 12.92, step(color, 0.04045));
}

float3 LinearToSrgb(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, step(color, 0.0031308));
}

float4 main(Input input) : SV_Target0
{
    // Leave UVs unwrapped for correct mip derivatives; the sampler repeats.
    float2 uv = input.worldXZ / material.x;
    float3 grass = baseColorTexture.Sample(baseColorSampler, uv).rgb;
    float3 surfaceColor = lerp(SrgbToLinear(input.color), grass, material.y);

    float3 detail = normalTexture.Sample(normalSampler, uv).rgb * 2.0 - 1.0;
    detail.xy *= material.z;
    detail.y *= material.w;
    detail.z = max(detail.z, 0.0001);
    detail = normalize(detail);

    // Heightfield frame: +U follows +world X, +V follows +world Z.
    // Gram-Schmidt gives an orthonormal frame even on sloping ground.
    float3 N = normalize(input.normal);
    float3 T = normalize(float3(N.y, -N.x, 0.0));
    float3 B = normalize(cross(T, N));
    float3 worldNormal = normalize(detail.x * T + detail.y * B + detail.z * N);

    // Keep the existing display-space lighting response and light intensities.
    // Base-color blending above is linear; terrain alpha is always opaque.
    return float4(LinearToSrgb(surfaceColor) * WorldBrightness(worldNormal), 1.0);
}
)hlsl";
