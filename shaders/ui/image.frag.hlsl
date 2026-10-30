Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
float4 main(Input input) : SV_Target0
{
    // Opacity scales both RGB and alpha for premultiplied pixels.
    return image.Sample(imageSampler, input.uv) * float4(input.tint.rgb * input.tint.a, input.tint.a);
}
