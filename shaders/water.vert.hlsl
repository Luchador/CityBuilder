#include "common/world_view.hlsli"
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
