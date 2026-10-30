Texture2D atlas : register(t0, space2);
SamplerState atlasSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(Input input) : SV_Target0
{
    // SDL_ttf supplies straight-alpha glyph images. The pipeline blends them.
    return atlas.Sample(atlasSampler, input.uv) * input.color;
}
