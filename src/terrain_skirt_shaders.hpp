#pragma once
#include "terrain_skirt.hpp"

// Keep the HLSL palette capacity in step with the CPU uniform layout.
static_assert(TerrainSkirtLayerCount == 40);

// Compile with WorldViewShaderSource (camera and shared world lighting).
inline constexpr char TerrainSkirtVertexShaderSource[] = R"hlsl(
struct Output {
    float4 position : SV_Position;
    float2 heights : TEXCOORD0;
    float brightness : TEXCOORD1;
};

Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1, float terrainHeight : TEXCOORD2)
{
    Output result;
    result.position = ProjectWorldPosition(position);
    result.heights = float2(position.y, terrainHeight);
    result.brightness = WorldBrightness(normal);
    return result;
}
)hlsl";

inline constexpr char TerrainSkirtFragmentShaderSource[] = R"hlsl(
cbuffer Rock : register(b0, space3)
{
    float4 rockSettings; // bottomY, inverse exponent, layer count, unused
    float4 rockColors[40]; // top to bottom
};

struct Input {
    float4 position : SV_Position;
    float2 heights : TEXCOORD0;
    float brightness : TEXCOORD1;
};

float4 main(Input input) : SV_Target0
{
    // Interpolate both heights first, then divide. Interpolating a normalized
    // depth instead would kink layer boundaries along a sloping quad's diagonal.
    float depth = saturate((input.heights.y - input.heights.x) /
                           (input.heights.y - rockSettings.x));
    float layerPosition = pow(depth, rockSettings.y) * rockSettings.z;
    uint layer = min((uint)layerPosition, (uint)rockSettings.z - 1);
    return float4(rockColors[layer].rgb * input.brightness, 1.0);
}
)hlsl";
