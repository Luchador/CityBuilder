#pragma once
#include "scene.hpp"
#include <atomic>
#include <stop_token>

namespace renderkit
{
    // Same world-space direction and display-space lighting response as shaders.hpp.
    // This vector points TOWARD the light, not along the emitted rays.
    inline constexpr Vec3 DirectionalLightDirection{-.75f,.5f,.25f};
    inline constexpr float LightingDivisor = .89f;
    struct Lighting
    {
        Vec3 ambientColor{1,1,1}, directionalColor{1,1,1};
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
    };
    struct RenderSettings
    {
        int width = 512, height = 512;
        float padding = .08f;
        bool autoFit = true;
        float pixelsPerUnit = 32.0f;
        Lighting lighting;
        AmbientOcclusion ambientOcclusion;
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
              std::uint32_t ignoredTriangle = std::numeric_limits<std::uint32_t>::max());
    struct RenderImage
    {
        int width = 0, height = 0;
        // Straight/unassociated alpha: exactly 255 on an accepted hit, 0 on miss.
        std::vector<std::uint8_t> rgba, albedo, directional;
    };
    RenderImage Render(const Scene& scene, const RenderSettings& settings, const Frame& frame,
                       unsigned quarterTurns, std::stop_token stop = {},
                       std::atomic<int>* completedRows = nullptr);
}
