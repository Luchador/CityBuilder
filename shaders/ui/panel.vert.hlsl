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
