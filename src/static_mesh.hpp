#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// CPU geometry, independent of SDL/GPU lifetime. Texture assignment is left to
// the renderer; this first importer combines primitives into one material.
struct StaticMeshVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};
static_assert(sizeof(StaticMeshVertex) == 32);

struct StaticMesh
{
    std::vector<StaticMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Static, uncompressed GLB triangles with normals and TEXCOORD_0. Bakes scene
// node transforms, including the normal inverse transpose and mirrored winding.
// Keeps the authored axes, pivot and UVs. unitsPerMeter applies to all axes.
StaticMesh LoadStaticGlb(const std::filesystem::path& path, float unitsPerMeter = 1.0f);
