#pragma once
#include "scene.hpp"

namespace renderkit
{
    // DX10 DDS: RGBA16_FLOAT, linear premultiplied RGB, no mipmaps. Alpha from the shared RGBA coverage.
    void SaveLightingDds(const std::filesystem::path& path,int width,int height,
        std::span<const Vec3> colors,std::span<const std::uint8_t> rgba);
}
