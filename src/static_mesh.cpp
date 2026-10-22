#include "static_mesh.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    struct Vec3 { float x, y, z; };
    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
    void Require(cgltf_result result, const char* operation)
    {
        if (result != cgltf_result_success)
            throw std::runtime_error(std::string(operation) + " (cgltf error " + std::to_string(result) + ")");
    }
}

StaticMesh LoadStaticGlb(const std::filesystem::path& path, float unitsPerMeter)
{
    if (!std::isfinite(unitsPerMeter) || unitsPerMeter <= 0)
        throw std::invalid_argument("Model scale must be finite and positive");
    // std::filesystem opens Unicode Windows paths; cgltf parses the in-memory
    // GLB, whose buffers must be embedded rather than fetched from other files.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto utf8 = path.u8string();
    const std::string label(utf8.begin(), utf8.end());
    if (!file) throw std::runtime_error("Cannot open model: " + label);
    const auto size = file.tellg();
    if (size <= 0 || size > 256 * 1024 * 1024)
        throw std::runtime_error("GLB must be nonempty and no larger than 256 MiB: " + label);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read model: " + label);

    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* raw = nullptr;
    const auto parsed = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    Require(parsed, "Parse GLB");
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
        if (data->buffers[i].uri) throw std::runtime_error("Export the model with embedded GLB buffers");
    Require(cgltf_load_buffers(&options, data.get(), nullptr), "Load GLB buffers");
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
        if (!data->buffers[i].data) throw std::runtime_error("GLB buffer is missing");
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
        if (data->buffer_views[i].has_meshopt_compression)
            throw std::runtime_error("Disable mesh compression when exporting the road model");
    Require(cgltf_validate(data.get()), "Validate GLB");

    StaticMesh mesh;
    constexpr cgltf_size MaxVertices = 2'000'000, MaxIndices = 6'000'000;
    std::vector<bool> visited(data->nodes_count, false);
    const auto visit = [&](auto&& self, const cgltf_node* node, unsigned int depth) -> void
    {
        if (depth > 128) throw std::runtime_error("GLB node hierarchy is too deep");
        const auto nodeIndex = static_cast<std::size_t>(node - data->nodes);
        if (visited[nodeIndex]) return;
        visited[nodeIndex] = true;
        // transform_world follows parents; check for cycles before calling it.
        const auto* parent = node;
        for (cgltf_size hops = 0; parent; ++hops, parent = parent->parent)
            if (hops > data->nodes_count) throw std::runtime_error("Cycle in GLB node hierarchy");
        if (node->skin || node->has_mesh_gpu_instancing)
            throw std::runtime_error("Bake skinned/instanced meshes before exporting a static model");
        if (node->mesh)
        {
            float m[16];
            cgltf_node_transform_world(node, m);
            const Vec3 a{m[0], m[1], m[2]}, b{m[4], m[5], m[6]}, c{m[8], m[9], m[10]};
            const Vec3 ca = Cross(b, c), cb = Cross(c, a), cc = Cross(a, b);
            const float determinant = Dot(a, ca);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20f)
                throw std::runtime_error("GLB mesh has a singular transform");

            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
            {
                const auto& primitive = node->mesh->primitives[p];
                if (primitive.type != cgltf_primitive_type_triangles ||
                    primitive.has_draco_mesh_compression || primitive.targets_count)
                    throw std::runtime_error("Export static, uncompressed triangle meshes without morph targets");
                const auto* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
                const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
                const auto* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
                if (!positions || !normals || !uvs || positions->type != cgltf_type_vec3 ||
                    normals->type != cgltf_type_vec3 || uvs->type != cgltf_type_vec2 ||
                    !positions->count || positions->count != normals->count || positions->count != uvs->count)
                    throw std::runtime_error("GLB requires matching positions, normals and TEXCOORD_0");
                const auto count = primitive.indices ? primitive.indices->count : positions->count;
                if (!count || count % 3 || positions->count > MaxVertices - mesh.vertices.size() ||
                    count > MaxIndices - mesh.indices.size())
                    throw std::runtime_error("Invalid or excessively large GLB triangle mesh");
                if (primitive.indices && (primitive.indices->is_sparse || primitive.indices->type != cgltf_type_scalar ||
                    (primitive.indices->component_type != cgltf_component_type_r_8u &&
                     primitive.indices->component_type != cgltf_component_type_r_16u &&
                     primitive.indices->component_type != cgltf_component_type_r_32u)))
                    throw std::runtime_error("GLB indices must be nonsparse unsigned integers");

                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (cgltf_size i = 0; i < positions->count; ++i)
                {
                    float pos[3], normal[3], uv[2];
                    if (!cgltf_accessor_read_float(positions, i, pos, 3) ||
                        !cgltf_accessor_read_float(normals, i, normal, 3) ||
                        !cgltf_accessor_read_float(uvs, i, uv, 2))
                        throw std::runtime_error("Cannot read GLB vertex data");
                    const Vec3 position{
                        (m[0] * pos[0] + m[4] * pos[1] + m[8] * pos[2] + m[12]) * unitsPerMeter,
                        (m[1] * pos[0] + m[5] * pos[1] + m[9] * pos[2] + m[13]) * unitsPerMeter,
                        (m[2] * pos[0] + m[6] * pos[1] + m[10] * pos[2] + m[14]) * unitsPerMeter};
                    Vec3 n{(ca.x * normal[0] + cb.x * normal[1] + cc.x * normal[2]) / determinant,
                           (ca.y * normal[0] + cb.y * normal[1] + cc.y * normal[2]) / determinant,
                           (ca.z * normal[0] + cb.z * normal[1] + cc.z * normal[2]) / determinant};
                    const float length = std::sqrt(Dot(n, n));
                    if (!Finite(position) || !Finite(n) || !std::isfinite(length) || length < 1e-20f ||
                        !std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                        throw std::runtime_error("GLB contains invalid positions, normals or UVs");
                    mesh.vertices.push_back({position.x, position.y, position.z,
                                             n.x / length, n.y / length, n.z / length, uv[0], uv[1]});
                }
                const auto firstIndex = mesh.indices.size();
                for (cgltf_size i = 0; i < count; ++i)
                {
                    const auto index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, i) : i;
                    if (index >= positions->count) throw std::runtime_error("GLB index is outside the vertex array");
                    mesh.indices.push_back(base + static_cast<std::uint32_t>(index));
                }
                if (determinant < 0)
                    for (auto i = firstIndex; i < mesh.indices.size(); i += 3)
                        std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
            }
        }
        for (cgltf_size i = 0; i < node->children_count; ++i) self(self, node->children[i], depth + 1);
    };
    const auto* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene)
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) visit(visit, scene->nodes[i], 0);
    else
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent) visit(visit, &data->nodes[i], 0);
    if (mesh.indices.empty()) throw std::runtime_error("GLB scene contains no triangles");
    return mesh;
}
