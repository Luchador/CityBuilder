// Shared lighting for grid, skirt, terrain, and roads.
float WorldBrightness(float3 normal)
{
    const float ambientIntensity = 0.55;
    const float directionalIntensity = 1.00;
    const float3 lightDirection = float3(-0.75, 0.5, 0.25);

    float sunlight = saturate(dot(normalize(normal), lightDirection));
    return (ambientIntensity + directionalIntensity * sunlight) / 0.89;
}
