#pragma once

// Shader Model 5.1 / DXBC 
// Shared by terrain and water so both use exactly the same camera/depth math.
inline constexpr char WorldViewShaderSource[] = R"hlsl(
cbuffer View : register(b0, space1)
{
    float4 camera; // center X, center Z, vertical half-span, aspect ratio
    float4 style;  // RGB tint, depth offset
};

float4 ProjectWorldPosition(float3 position)
{
    float3 p = position - float3(camera.x, 0.0, camera.y);

    // Orthographic camera: 45-degree yaw, 33-degree pitch.
    float right = dot(p, float3(0.70710678, 0.0, -0.70710678));
    float up = dot(p, float3(-0.38511795, 0.83867057, -0.38511795));
    float towardCamera = dot(p, float3(0.59302965, 0.54463904, 0.59302965));

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

inline constexpr char PanVertexShaderSource[] = R"hlsl(
cbuffer Sprite : register(b0, space1)
{
    float4 screen;
    float4 rectangle; // center X/Y, half width/height in window coordinates
    float4 transform; // row-major 2x2 screen-space transform
};

struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Output main(uint vertex : SV_VertexID)
{
    const float2 corners[6] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(1, -1), float2(1, 1), float2(-1, 1)
    };
    float2 corner = corners[vertex];
    float2 local = corner * rectangle.zw;
    float2 projected = float2(dot(transform.xy, local), dot(transform.zw, local));
    float2 pixel = rectangle.xy + projected;
    Output result;
    result.position = float4(pixel.x / screen.x * 2 - 1,
                             1 - pixel.y / screen.y * 2, 0, 1);
    result.uv = (corner + 1) * 0.5;
    return result;
}
)hlsl";

inline constexpr char PanFragmentShaderSource[] = R"hlsl(
Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0
{
    return image.Sample(imageSampler, input.uv);
}
)hlsl";
