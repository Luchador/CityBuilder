#pragma once

inline constexpr char UiImageVertexShaderSource[] = R"hlsl(
cbuffer Image : register(b0, space1)
{
    float4 screen;
    float4 rectangle; // Center X/Y, half width/height, in logical window units.
    float4 transform;
    float4 tint;
};
struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
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
    result.position = float4(pixel.x / screen.x * 2 - 1, 1 - pixel.y / screen.y * 2, 0, 1);
    result.uv = (corner + 1) * 0.5;
    result.tint = tint;
    return result;
}
)hlsl";

inline constexpr char UiToolButtonFragmentShaderSource[] = R"hlsl(
Texture2D icon : register(t0, space2);
Texture2D opacity : register(t1, space2);
Texture2D overlay : register(t2, space2);
SamplerState iconSampler : register(s0, space2);
SamplerState opacitySampler : register(s1, space2);
SamplerState overlaySampler : register(s2, space2);
cbuffer ToolState : register(b0, space3)
{
    float4 options; // x = 1 for an overlay, 0 for the normal state.
};
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
float4 main(Input input) : SV_Target0
{
    float4 base = icon.Sample(iconSampler, input.uv);
    float4 effect = overlay.Sample(overlaySampler, input.uv) * options.x;
    float coverage = saturate(opacity.Sample(opacitySampler, input.uv).r);
    // UI images are already premultiplied. Alpha-over preserves both the white
    // highlights and dark bevels in the supplied overlays.
    float4 color = (effect + base * (1.0 - effect.a)) * coverage;
    return color * float4(input.tint.rgb * input.tint.a, input.tint.a);
}
)hlsl";

inline constexpr char UiImageFragmentShaderSource[] = R"hlsl(
Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
float4 main(Input input) : SV_Target0
{
    // Opacity scales both RGB and alpha for premultiplied pixels.
    return image.Sample(imageSampler, input.uv) * float4(input.tint.rgb * input.tint.a, input.tint.a);
}
)hlsl";
