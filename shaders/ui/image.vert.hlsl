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
