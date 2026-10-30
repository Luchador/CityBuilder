Texture2D icon : register(t0, space2);
Texture2D opacity : register(t1, space2);
Texture2D overlay : register(t2, space2);
SamplerState iconSampler : register(s0, space2);
SamplerState opacitySampler : register(s1, space2);
SamplerState overlaySampler : register(s2, space2);
cbuffer ToolState : register(b0, space3)
{
    float4 options; // x = 1 for an overlay, 0 for the normal state.
};
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
float4 main(Input input) : SV_Target0
{
    float4 base = icon.Sample(iconSampler, input.uv);
    float4 effect = overlay.Sample(overlaySampler, input.uv) * options.x;
    float coverage = saturate(opacity.Sample(opacitySampler, input.uv).r);
    // UI images are already premultiplied. Alpha-over preserves both the white
    // highlights and dark bevels in the supplied overlays.
    float4 color = (effect + base * (1.0 - effect.a)) * coverage;
    return color * float4(input.tint.rgb * input.tint.a, input.tint.a);
}
