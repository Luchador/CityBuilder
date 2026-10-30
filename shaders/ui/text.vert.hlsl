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
