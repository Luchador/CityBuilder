#pragma once
#include "render.hpp"

namespace renderkit
{
    // Shared material evaluation uses the same UVs, PNG sampling and vertex colors as legacy rendering.
    struct Surface
    {
        Vec3 point, geometric, normal, albedo;
        std::uint32_t triangle = std::numeric_limits<std::uint32_t>::max();
    };
    Surface EvaluateSurface(const Scene& scene, const Hit& hit, Vec3 incoming);
    RenderImage RenderGlobalIllumination(const Scene& scene, const RenderSettings& settings, const Frame& frame,
        unsigned quarterTurns, std::stop_token stop, std::atomic<int>* completedRows);
}
