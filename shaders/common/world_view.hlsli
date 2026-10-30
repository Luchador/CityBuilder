// Shared by all world vertex shaders for identical camera and depth math.
cbuffer View : register(b0, space1)
{
    float4 camera; // center X, center Z, vertical half-span, aspect ratio
    float4 style;  // RGB tint, depth offset
    float4 orientation; // cos(yaw), sin(yaw), sin(pitch), cos(pitch)
    float4 world; // map extent, padding
};

float4 ProjectWorldPosition(float3 position)
{
    float3 p = position - float3(camera.x, 0.0, camera.y);

    // Camera supplies one of four yaw orientations and the fixed pitch.
    float cosYaw = orientation.x;
    float sinYaw = orientation.y;
    float sinPitch = orientation.z;
    float cosPitch = orientation.w;
    float right = dot(p, float3(cosYaw, 0.0, -sinYaw));
    float up = dot(p, float3(-sinPitch * sinYaw, cosPitch, -sinPitch * cosYaw));
    float towardCamera = dot(p, float3(cosPitch * sinYaw, sinPitch, cosPitch * cosYaw));

    // Keep large maps inside the depth range even from an edge/corner,
    // including elevated terrain and the skirt. All world passes share this.
    float depthSpan = max(2048.0, 4.0 * world.x);
    return float4(right / (camera.z * camera.w), up / camera.z, 0.5 - towardCamera / depthSpan + style.w, 1.0);
}
