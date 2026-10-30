// Locations 3 (pose) and 4 (slope/length/UV mode) advance once per road instance.
#include "common/world_view.hlsli"
struct Output {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
    nointerpolation float repeatAlongRoad : TEXCOORD3;
};
float2 RotateRoad(float2 value, float turns)
{
    // The mesh keeps its width. Half turns rotate the lengthened straight/end
    // by 45 degrees, after which the existing exact quarter turns still apply.
    if (frac(turns) > 0.25)
        value = float2(value.x - value.y, value.x + value.y) * 0.707106781187;
    turns = floor(turns);
    if (turns < 0.5) return value;
    if (turns < 1.5) return float2(-value.y, value.x);
    if (turns < 2.5) return -value;
    return float2(value.y, -value.x);
}
Output main(float3 position : TEXCOORD0, float3 normal : TEXCOORD1,
            float2 uv : TEXCOORD2, float4 instance : TEXCOORD3, float4 surface : TEXCOORD4)
{
    Output result;
    position.x *= surface.z;
    normal.x /= surface.z; // Inverse transpose of the nonuniform length scale.
    position.xz = RotateRoad(position.xz, instance.w);
    normal.xz = RotateRoad(normal.xz, instance.w);
    // Shear vertically onto the terrain plane: the tile footprint stays fixed,
    // so neighboring pieces meet without shrinking/stretching their X/Z span.
    position.y += dot(surface.xy, position.xz);
    // Inverse transpose of the shear, after the mesh's yaw rotation.
    normal.xz -= surface.xy * normal.y;
    position += instance.xyz;
    result.position = ProjectWorldPosition(position);
    result.normal = normal;
    result.uv = uv;
    result.repeatAlongRoad = surface.w;
    if (surface.w > 0.5)
        result.uv.y = (uv.y - 0.5) * surface.z + dot(instance.xz, RotateRoad(float2(1, 0), instance.w));
    result.worldXZ = position.xz;
    return result;
}
