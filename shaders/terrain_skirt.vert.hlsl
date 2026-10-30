#include "common/world_view.hlsli"
#include "common/world_lighting.hlsli"
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
