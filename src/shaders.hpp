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
