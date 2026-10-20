#pragma once
#include "render.hpp"

namespace renderkit
{
    // Creates a new numbered folder. Existing exports are never overwritten.
    std::filesystem::path ExportViews(const Scene& scene,const RenderSettings& settings,
        const std::filesystem::path& parent,const std::string& prefix,bool lightingLayers,
        std::stop_token stop={},std::atomic<int>* completedRows=nullptr);
}
