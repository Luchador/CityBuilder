#pragma once

// Shader Model 5.1 / DXBC 
// Shared by terrain, grid, water, and skirt for identical camera/depth math.
inline constexpr char WorldViewShaderSource[] = R"hlsl(
cbuffer View : register(b0, space1)
{
    float4 camera; // center X, center Z, vertical half-span, aspect ratio
    float4 style;  // RGB tint, depth offset
    float4 orientation; // cos(yaw), sin(yaw), sin(pitch), cos(pitch)
};

float4 ProjectWorldPosition(float3 position)
{
    float3 p = position - float3(camera.x, 0.0, camera.y);

    // Camera supplies one of four yaw orientations and the fixed pitch.
    float cosYaw = orientation.x;
    float sinYaw = orientation.y;
    float sinPitch = orientation.z;
    float cosPitch = orientation.w;
    float right = dot(p, float3(cosYaw, 0.0, -sinYaw));
    float up = dot(p, float3(-sinPitch * sinYaw, cosPitch, -sinPitch * cosYaw));
    float towardCamera = dot(p, float3(cosPitch * sinYaw, sinPitch, cosPitch * cosYaw));

    return float4(right / (camera.z * camera.w), up / camera.z, 0.5 - towardCamera / 2048.0 + style.w, 1.0);
}

float WorldBrightness(float3 normal)
{
    const float ambientIntensity = 0.45;
    const float directionalIntensity = 1.00;
    const float3 lightDirection = float3(-0.6, 0.5, 0.0);

    float sunlight = saturate(dot(normalize(normal), lightDirection));
    return (ambientIntensity + directionalIntensity * sunlight) / 0.89;
}
)hlsl";

inline constexpr char VertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1, float3 vertexColor : TEXCOORD2)
{
    Output result;
    result.position = ProjectWorldPosition(position);

    result.color = float4(vertexColor * style.rgb * WorldBrightness(normal), 1.0);

    return result;
}
)hlsl";

inline constexpr char UiVertexShaderSource[] = R"hlsl(
cbuffer Screen : register(b0, space1)
{
    float4 screen; // logical window width, height, unused, unused
};

struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(float2 position : TEXCOORD0, float4 color : TEXCOORD1)
{
    Output result;
    result.position = float4(position.x / screen.x * 2.0 - 1.0,
                             1.0 - position.y / screen.y * 2.0, 0.0, 1.0);
    result.color = color;
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
