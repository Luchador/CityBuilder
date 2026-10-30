#include "common/world_view.hlsli"
#include "common/world_lighting.hlsli"
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
