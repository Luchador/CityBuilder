#define ZONE_TEXTURE t0
#define ZONE_SAMPLER s0
#define ROAD_TEXTURE t1
#define ROAD_SAMPLER s1
#define LOT_TEXTURE t2
#define LOT_SAMPLER s2
#define ARROW_TEXTURE t3
#define ARROW_SAMPLER s3
#define ZONE_UNIFORMS b0
#include "common/zone_overlay.hlsli"
struct Input {
    float4 position : SV_Position;
    float waterDepth : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};

float4 main(Input input) : SV_Target0
{
    // A shore tile can contain a whole dry triangle at Y=0. Reject it explicitly
    // to avoid coplanar flicker/blue patches; depth testing also hides water behind land.
    if (input.waterDepth <= 0.0) discard;
    return float4(ApplyZoneOverlay(input.color.rgb, input.worldXZ, false), 1.0);
}
