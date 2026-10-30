// UVs come from world position, so grass stays fixed during camera movement.
#include "common/world_view.hlsli"
struct Output {
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1, float3 vertexColor : TEXCOORD2)
{
    Output result;
    result.position = ProjectWorldPosition(position);
    result.color = vertexColor * style.rgb;
    result.normal = normal;
    result.worldXZ = position.xz;
    return result;
}
