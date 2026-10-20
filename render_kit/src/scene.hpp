#pragma once

#include "math.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace renderkit
{
    std::string Utf8Path(const std::filesystem::path& path);
    std::filesystem::path NativePath(const std::string& utf8);

    enum class Wrap { Repeat, Clamp, Mirror };
    struct Image
    {
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
        std::string name;
        // Empty for images embedded in the model; external PNGs retain their path.
        std::filesystem::path sourcePath;
        Vec4 Sample(Vec2 uv, Wrap u = Wrap::Repeat, Wrap v = Wrap::Repeat, bool linearRgb = false) const;
    };
    std::shared_ptr<const Image> LoadPng(const std::filesystem::path& path);
    std::shared_ptr<const Image> DecodePng(std::span<const std::uint8_t> bytes, const std::string& name);
    void SavePng(const std::filesystem::path& path, int width, int height, std::span<const std::uint8_t> rgba);

    enum class AlphaMode { Opaque, Cutout };
    enum class OpacityChannel { Luminance, Alpha, Red };
    struct Material
    {
        std::string name = "Default";
        Vec4 baseFactor{}; // Linear factors; image RGB is sRGB, alpha is linear.
        std::shared_ptr<const Image> baseColor, opacity;
        AlphaMode alphaMode = AlphaMode::Opaque;
        OpacityChannel opacityChannel = OpacityChannel::Luminance;
        float cutoff = .5f;
        bool doubleSided = false;
        int texcoord = 0;
        Vec2 uvOffset{}, uvScale{1,1};
        float uvRotation = 0;
        Wrap wrapU = Wrap::Repeat, wrapV = Wrap::Repeat;
        bool useBaseColorAlpha = true;
        bool invertOpacity = false;
        float opacityStrength = 1;
        // FBX imports bake each texture's named UV set/transform into slots 0/1.
        // GLB opacity maps reuse the base-color mapping by default.
        bool separateOpacityUv = false;
        Wrap opacityWrapU = Wrap::Repeat, opacityWrapV = Wrap::Repeat;
    };
    struct Vertex
    {
        Vec3 position, normal;
        std::array<Vec2,2> uv{};
        Vec4 color{};
    };
    struct Triangle
    {
        std::array<Vertex,3> vertices;
        std::uint32_t material = 0;
    };
    struct BvhNode
    {
        Bounds bounds;
        std::uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    struct Geometry
    {
        std::vector<Triangle> triangles;
        Bounds bounds;
        std::vector<std::uint32_t> order;
        std::vector<BvhNode> nodes;
        void BuildAcceleration();
    };
    struct Scene
    {
        std::shared_ptr<const Geometry> geometry;
        std::vector<Material> materials;
        std::string source;
        std::vector<std::string> warnings;
    };
    Scene LoadGlb(const std::filesystem::path& path);
    Scene LoadFbx(const std::filesystem::path& path);
    Scene LoadModel(const std::filesystem::path& path);
}
