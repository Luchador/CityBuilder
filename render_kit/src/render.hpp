#pragma once
#include "scene.hpp"
#include <atomic>
#include <stop_token>

namespace renderkit
{
    // Same world-space direction as shaders.hpp. Legacy mode retains its magnitude; GI normalizes it.
    // This vector points TOWARD the light, not along the emitted rays.
    inline constexpr Vec3 DirectionalLightDirection{-.75f,.5f,.25f};
    inline constexpr float LightingDivisor = .89f;
    struct Lighting
    {
        Vec3 ambientColor{1,1,1}, directionalColor{1,1,1}; // sRGB UI colors; converted to linear in GI mode.
        float ambientIntensity = .55f, directionalIntensity = 1.0f;
        bool shadows = true;
    };
    struct AmbientOcclusion
    {
        // Baked into linear base color before sRGB encoding, never into alpha/sun.
        bool enabled = false;
        float intensity = .75f; // 0 = unchanged, 1 = full occlusion strength.
        float radius = 1.0f; // Maximum ray distance in imported model units.
        int samples = 64;
        bool useGroundPlane = false; // Invisible AO-only plane at the model's minimum Y.
    };
    struct RenderSettings
    {
        int width = 512, height = 512;
        float padding = .08f;
        bool autoFit = true;
        float pixelsPerUnit = 32.0f;
        Lighting lighting;
        AmbientOcclusion ambientOcclusion;
        int aaGridSize = 1; // 1=off, 2/4/8 samples per axis (4/16/64 rays per pixel).
        struct GlobalIllumination
        {
            bool enabled = false; // Legacy scenes retain their original rendering.
            int samples = 64, previewSamples = 16; // Paths per covered AA sample.
            int bounces = 2; // Additional diffuse surface hits after the camera hit.
            float strength = 1; // Directional indirect-light multiplier, applied after baking.
            bool useGroundPlane = true;
            Vec3 groundColor{.2f,.2f,.2f}; // Linear diffuse reflectance; invisible to camera rays.
        } globalIllumination;
        void Validate() const;
    };
    struct Basis { Vec3 right, up, towardCamera; };
    Basis ViewBasis(unsigned quarterTurns);
    struct Frame
    {
        Vec3 center, pivot;
        float halfHeight = 1, distance = 1, pixelsPerUnit = 1;
        std::array<Vec2,4> pivotPixels{};
    };
    Frame FitFrame(const Geometry& geometry, const RenderSettings& settings);
    struct Hit
    {
        bool found = false;
        float distance = 0;
        std::uint32_t triangle = 0;
        float u = 0, v = 0;
    };
    Hit Trace(const Scene& scene, Vec3 origin, Vec3 direction, float maxDistance,
              bool shadowRay = false,
              std::uint32_t ignoredTriangle = std::numeric_limits<std::uint32_t>::max(),
              bool twoSided = false); // Nearest two-sided hit for diffuse paths (not any-hit shadows).
    struct RenderImage
    {
        int width = 0, height = 0;
        // Straight/unassociated RGB; alpha is sample coverage (binary with AA off).
        std::vector<std::uint8_t> rgba, albedo, directional;
        // Unit-white-light response, including material color. Linear HDR, straight RGB.
        // Empty in legacy mode. All layers share coverage from rgba's alpha channel.
        std::vector<Vec3> directLight, indirectLight, skyLight;
    };
    // Recombine a completed GI bake without tracing rays. Colors in Lighting are sRGB UI values.
    void Relight(RenderImage& image, const Lighting& lighting, float giStrength);
    std::vector<std::uint8_t> LightingLayerPreview(const RenderImage& image, std::span<const Vec3> layer);
    RenderImage Render(const Scene& scene, const RenderSettings& settings, const Frame& frame,
                       unsigned quarterTurns, std::stop_token stop = {},
                       std::atomic<int>* completedRows = nullptr);
}
