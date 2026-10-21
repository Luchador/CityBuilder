#pragma once

// Compile with WorldViewShaderSource from shaders.hpp as the vertex preamble.
inline constexpr char WaterVertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float waterDepth : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

Output main(float3 position : TEXCOORD0, float waterDepth : TEXCOORD1)
{
    Output result;
    result.position = ProjectWorldPosition(position);
    result.waterDepth = waterDepth;
    result.worldXZ = position.xz;
    result.color = float4(style.rgb, 1.0);
    return result;
}
)hlsl";

inline constexpr char WaterFragmentShaderSource[] = R"hlsl(
struct Input {
    float4 position : SV_Position;
    float waterDepth : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

float4 main(Input input) : SV_Target0
{
    // A shore tile can contain a whole dry triangle at Y=0. Reject it explicitly
    // to avoid coplanar flicker/blue patches; depth testing also hides water behind land.
    if (input.waterDepth <= 0.0) discard;
    return float4(ApplyZoneOverlay(input.color.rgb, input.worldXZ, false), 1.0);
}
)hlsl";
