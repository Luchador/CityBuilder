#pragma once

// Shader Model 5.1 / DXBC 
inline constexpr char VertexShaderSource[] = R"hlsl(
cbuffer View : register(b0, space1)
{
    float4 camera; // center X, center Z, vertical half-span, aspect ratio
    float4 style;  // RGB tint, depth offset
};

struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(float3 position : TEXCOORD0)
{
    float3 p = position - float3(camera.x, 0.0, camera.y);

    // Orthonormal isometric camera: 45-degree yaw, ~35.264-degree pitch.
    float right = dot(p, float3(0.70710678, 0.0, -0.70710678));
    float up = dot(p, float3(-0.40824829, 0.81649658, -0.40824829));
    float towardCamera = dot(p, float3(0.57735027, 0.57735027, 0.57735027));

    Output result;

    result.position = float4(right / (camera.z * camera.w), up / camera.z, 0.5 - towardCamera / 2048.0 + style.w, 1.0);
    result.color = float4(style.rgb, 1.0);
    return result;
}
)hlsl";

inline constexpr char FragmentShaderSource[] = R"hlsl(
struct Input {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

float4 main(Input input) : SV_Target0
{
    return input.color;
}
)hlsl";
