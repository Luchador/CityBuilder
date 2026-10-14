#pragma once

inline constexpr char TextVertexShaderSource[] = R"hlsl(
cbuffer Screen : register(b0, space1)
{
    float4 screen; // Logical width and height, unused, unused.
};

struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

Output main(float2 position : TEXCOORD0, float2 uv : TEXCOORD1, float4 color : TEXCOORD2)
{
    Output result;
    result.position = float4(position.x / screen.x * 2 - 1,
                             1 - position.y / screen.y * 2, 0, 1);
    result.uv = uv;
    result.color = color;
    return result;
}
)hlsl";

inline constexpr char TextFragmentShaderSource[] = R"hlsl(
Texture2D atlas : register(t0, space2);
SamplerState atlasSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(Input input) : SV_Target0
{
    // SDL_ttf supplies straight-alpha glyph images. The pipeline blends them.
    return atlas.Sample(atlasSampler, input.uv) * input.color;
}
)hlsl";
