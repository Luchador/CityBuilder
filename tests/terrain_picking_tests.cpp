#include "terrain_picking.hpp"
#include "camera.hpp"
#include "terrain.hpp"
#include "terrain_skirt.hpp"
#include "water.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Point { double x, y, z; };
    struct Screen { double x, y, depth; };

    // Independent forward projection, using the exact shader coefficients.
    Screen Project(Point point, const Camera& camera, const PickingViewport& view)
    {
        const auto o = camera.GetOrientation();
        const double x = point.x - camera.GetCenterX(), z = point.z - camera.GetCenterZ();
        const double right = x * o.cosYaw - z * o.sinYaw;
        const double up = -x * o.sinPitch * o.sinYaw + point.y * o.cosPitch - z * o.sinPitch * o.cosYaw;
        const double toward = x * o.cosPitch * o.sinYaw + point.y * o.sinPitch + z * o.cosPitch * o.cosYaw;
        const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
        return {(view.x + (right / (camera.GetHalfHeight() * static_cast<double>(aspect)) + 1) * view.width * 0.5) / view.pixelsPerWindowX,
                (view.y + (1 - up / camera.GetHalfHeight()) * view.height * 0.5) / view.pixelsPerWindowY,
                0.5 - toward / std::max(2048.0, 4.0 * camera.GetMapExtent())};
    }

    std::optional<ToolHit> PickPoint(const TerrainHeightmap& terrain, const Camera& camera,
                                   const PickingViewport& view, Point point, TerrainPickOptions options = {})
    {
        const auto screen = Project(point, camera, view);
        return PickMapTile(terrain, camera, view, static_cast<float>(screen.x), static_cast<float>(screen.y), options);
    }

    struct ReferenceHit
    {
        Point point;
        double depth = 2;
        ToolHitSurface surface = ToolHitSurface::Terrain;
        bool skirt = false;
    };

    // Slow independent oracle: project the actual generated mesh, find screen
    // triangles containing the cursor by barycentrics, and resolve GPU depth.
    // It does not use cursor rays, grid traversal, or the picker's intersections.
    struct ReferenceScene
    {
        TerrainMesh terrain;
        WaterMesh water;
        TerrainSkirtMesh skirt;

        explicit ReferenceScene(const TerrainHeightmap& heights)
            : terrain(BuildTerrain(heights)), water(BuildWater(heights)), skirt(BuildTerrainSkirt(heights)) {}

        std::optional<ReferenceHit> Pick(const Camera& camera, const PickingViewport& view,
                                         float mouseX, float mouseY, bool withWater, bool withSkirt) const
        {
            if (mouseX * view.pixelsPerWindowX < view.x || mouseY * view.pixelsPerWindowY < view.y ||
                mouseX * view.pixelsPerWindowX >= view.x + view.width ||
                mouseY * view.pixelsPerWindowY >= view.y + view.height) return std::nullopt;
            std::optional<ReferenceHit> nearest;
            const auto triangle = [&](Point a, Point b, Point c, double waterA, double waterB, double waterC,
                                      ToolHitSurface surface, bool rock)
            {
                const auto sa = Project(a, camera, view), sb = Project(b, camera, view), sc = Project(c, camera, view);
                const double area = (sb.x - sa.x) * (sc.y - sa.y) - (sb.y - sa.y) * (sc.x - sa.x);
                if (area >= -1.0e-12) return; // Screen Y points down: negative area is front-facing.
                const double u = ((mouseX - sa.x) * (sc.y - sa.y) - (mouseY - sa.y) * (sc.x - sa.x)) / area;
                const double v = ((sb.x - sa.x) * (mouseY - sa.y) - (sb.y - sa.y) * (mouseX - sa.x)) / area;
                const double w = 1 - u - v;
                if (u < -1.0e-9 || v < -1.0e-9 || w < -1.0e-9) return;
                if (surface == ToolHitSurface::Water && w * waterA + u * waterB + v * waterC <= 0) return;
                const double depth = w * sa.depth + u * sb.depth + v * sc.depth;
                if (depth < 0 || depth > 1 || (nearest && depth >= nearest->depth)) return;
                nearest = ReferenceHit{{w * a.x + u * b.x + v * c.x,
                                        w * a.y + u * b.y + v * c.y,
                                        w * a.z + u * b.z + v * c.z}, depth, surface, rock};
            };
            const auto mesh = [&](const auto& source, ToolHitSurface surface, bool rock)
            {
                for (std::size_t i = 0; i < source.indices.size(); i += 3)
                {
                    const auto& a = source.vertices[source.indices[i]];
                    const auto& b = source.vertices[source.indices[i + 1]];
                    const auto& c = source.vertices[source.indices[i + 2]];
                    double wa = 0, wb = 0, wc = 0;
                    if constexpr (requires { a.waterDepth; })
                    {
                        wa = a.waterDepth; wb = b.waterDepth; wc = c.waterDepth;
                    }
                    triangle({a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, wa, wb, wc, surface, rock);
                }
            };
            if (withSkirt) mesh(skirt, ToolHitSurface::Terrain, true);
            mesh(terrain, ToolHitSurface::Terrain, false);
            if (withWater) mesh(water, ToolHitSurface::Water, false);
            return nearest;
        }
    };

    void CheckAgainstMesh(const TerrainHeightmap& heights, const ReferenceScene& scene,
                          const Camera& camera, const PickingViewport& viewport,
                          float x, float y, TerrainPickOptions options, unsigned int& matches)
    {
        auto expected = scene.Pick(camera, viewport, x, y, options.includeWater, options.skirtBottomY.has_value());
        if (expected && expected->skirt) expected.reset();
        const auto actual = PickMapTile(heights, camera, viewport, x, y, options);
        if (actual.has_value() != expected.has_value())
        {
            std::cerr << "Mismatch: yaw=" << camera.GetQuarterTurns() << " cursor=" << x << ',' << y
                      << " expected=" << expected.has_value() << " actual=" << actual.has_value() << '\n';
            throw std::runtime_error("Picker disagrees with rendered mesh coverage/occlusion");
        }
        if (!expected) return;
        Check(actual->surface == expected->surface, "Incorrect terrain/water surface");
        const auto& p = expected->point;
        Check(std::abs(actual->worldX - p.x) < 0.0002 && std::abs(actual->worldY - p.y) < 0.0002 &&
              std::abs(actual->worldZ - p.z) < 0.0002, "Hit position differs from rendered triangle");
        // Exact shared edges belong to either adjacent triangle in the reference.
        if (std::abs(p.x - std::round(p.x)) > 0.00001 && std::abs(p.z - std::round(p.z)) > 0.00001)
        {
            Check(actual->tile.x == static_cast<int>(std::floor(p.x)) && actual->tile.z == static_cast<int>(std::floor(p.z)),
                  "Incorrect map tile");
        }
        ++matches;
    }

    void CheckMeshAgreement()
    {
        TerrainHeightmap heights(MapDimensions(24));
        for (unsigned int z = 0; z <= 24; ++z)
            for (unsigned int x = 0; x <= 24; ++x)
            {
                // Cliffs, nonplanar quads, slopes facing away, and a mixed shoreline.
                heights.heights[z * 25 + x] = static_cast<float>(6 * std::sin(x * 0.55) + 5 * std::cos(z * 0.71) +
                    (x >= 9 && x <= 12 ? 14 : 0));
            }
        const ReferenceScene scene(heights);
        std::mt19937 random(9133);
        unsigned int matches = 0;
        for (unsigned int rotation = 0; rotation < 4; ++rotation)
        {
            for (float scale : {1.0f, 1.25f, 1.5f, 2.0f})
            {
                const PickingViewport view{17, 29, 767, 503, scale, scale};
                Camera camera(24);
                for (unsigned int i = 0; i < rotation; ++i) camera.RotateClockwise();
                camera.Zoom(8);
                for (int zoom = 0; zoom < 2; ++zoom)
                {
                    for (int sample = 0; sample < 220; ++sample)
                    {
                        const float sx = static_cast<float>(17.0 + (random() % 76700) / 100.0) / scale;
                        const float sy = static_cast<float>(29.0 + (random() % 50300) / 100.0) / scale;
                        CheckAgainstMesh(heights, scene, camera, view, sx, sy, {true, -40.0f}, matches);
                        CheckAgainstMesh(heights, scene, camera, view, sx, sy, {false, std::nullopt}, matches);
                    }
                    camera.Zoom(7);
                    camera.SetPosition(9, 14);
                }
            }
        }
        Check(matches > 500, "Coverage samples did not exercise enough visible terrain");
        std::cout << "PASS: " << matches << " visible hits agree with independent mesh projection/depth\n";
    }

    void CheckMapSizesAndEdges()
    {
        const PickingViewport scaled{11, 7, 1501, 913, 1.25f, 1.5f};
        for (unsigned int n : {128u, 256u, 512u, 1024u})
        {
            TerrainHeightmap heights{MapDimensions(n)};
            std::fill(heights.heights.begin(), heights.heights.end(), 0.25f);
            Camera camera(static_cast<float>(n));
            for (unsigned int rotation = 0; rotation < 4; ++rotation)
            {
                for (int zoom = 0; zoom < 2; ++zoom)
                {
                    for (unsigned int tile : {0u, n / 2, n - 1})
                    {
                        const float p = tile + 0.375f;
                        camera.SetPosition(p, p);
                        const auto hit = PickPoint(heights, camera, scaled, {p, 0.25, p});
                        Check(hit && hit->tile == ToolTile{static_cast<int>(tile), static_cast<int>(tile)} &&
                              std::abs(hit->worldY - 0.25f) < 0.0001f, "Wrong tile on a supported map size/zoom/rotation");
                    }
                    camera.Zoom(10);
                    camera.Zoom(10);
                }
                camera.ResetZoom();
                camera.RotateClockwise();
            }
        }

        TerrainHeightmap flat(MapDimensions(16));
        const PickingViewport view{0, 0, 640, 480};
        Camera camera(16);
        camera.Zoom(10);
        for (int rotation = 0; rotation < 4; ++rotation)
        {
            for (int corner : {0, 8, 16})
            {
                camera.SetPosition(static_cast<float>(corner), static_cast<float>(corner));
                const auto hit = PickMapTile(flat, camera, view, 320, 240);
                Check(hit && hit->tile == ToolTile{std::min(corner, 15), std::min(corner, 15)}, "Shared/outer edge tile rule failed");
            }
            Check(!PickPoint(flat, camera, view, {16.25, 0, 16.25}), "Off-map cursor clamped onto an edge tile");
            camera.RotateClockwise();
        }

        // Maximum elevation remains pickable through the shader's depth range.
        TerrainHeightmap high(MapDimensions(1024));
        std::fill(high.heights.begin(), high.heights.end(), TerrainMaxElevation);
        Camera large(1024);
        large.Zoom(-10);
        const auto hit = PickPoint(high, large, scaled, {512.25, 256, 512.25});
        Check(hit && hit->tile == ToolTile{512, 512} && std::abs(hit->worldY - 256) < 0.001,
              "Maximum elevation was clipped or projected incorrectly");
        std::cout << "PASS: all map sizes/rotations, close zoom, DPI scaling, borders, and maximum elevation\n";
    }

    void CheckWaterAndSkirt()
    {
        TerrainHeightmap lake(MapDimensions(24));
        std::fill(lake.heights.begin(), lake.heights.end(), -4.0f);
        Camera camera(24);
        camera.Zoom(8);
        const PickingViewport view{0, 0, 960, 720};
        const auto water = PickPoint(lake, camera, view, {12.3, 0, 12.3});
        const auto ground = PickPoint(lake, camera, view, {12.3, 0, 12.3}, {false, std::nullopt});
        Check(water && ground && water->surface == ToolHitSurface::Water && water->worldY == 0 &&
              ground->surface == ToolHitSurface::Terrain && std::abs(ground->worldY + 4) < 0.0001 &&
              water->tile != ground->tile, "Water must select its surface tile, not the shifted lake-bed tile");

        // A basin whose near skirt can hide distant ground. Check against the
        // actual skirt mesh, not just the terrain rectangle silhouette.
        for (unsigned int z = 0; z <= 24; ++z)
            for (unsigned int x = 0; x <= 24; ++x)
                lake.heights[z * 25 + x] = (x == 0 || z == 0 || x == 24 || z == 24) ? 4.0f : -8.0f;
        const ReferenceScene scene(lake);
        unsigned int hidden = 0, matches = 0;
        for (int rotation = 0; rotation < 4; ++rotation)
        {
            for (int y = 180; y < 620; y += 13)
                for (int x = 160; x < 800; x += 17)
                {
                    const float sx = static_cast<float>(x), sy = static_cast<float>(y);
                    CheckAgainstMesh(lake, scene, camera, view, sx, sy, {false, -40.0f}, matches);
                    if (PickMapTile(lake, camera, view, sx, sy, {false, std::nullopt}) &&
                        !PickMapTile(lake, camera, view, sx, sy, {false, -40.0f})) ++hidden;
                }
            camera.RotateClockwise();
        }
        Check(hidden > 0, "Skirt occlusion was not exercised");
        std::cout << "PASS: lake surface/bed distinction and " << hidden << " skirt-occluded hits\n";
    }

    void CheckInvalidInput()
    {
        TerrainHeightmap terrain(MapDimensions(4));
        Camera camera(4);
        PickingViewport view{0, 0, 640, 480};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(!PickMapTile(terrain, camera, view, nan, 240) && !PickMapTile(terrain, camera, view, 320, nan), "NaN cursor accepted");
        Check(!PickMapTile(terrain, camera, view, -1, 240) && !PickMapTile(terrain, camera, view, 640, 240) &&
              !PickMapTile(terrain, camera, view, 320, 480), "Outside viewport accepted");
        view.width = 0;
        Check(!PickMapTile(terrain, camera, view, 320, 240), "Zero-size viewport accepted");
        view.width = 640;
        view.pixelsPerWindowY = nan;
        Check(!PickMapTile(terrain, camera, view, 320, 240), "Invalid pixel scale accepted");
        view.pixelsPerWindowY = 1;
        Check(!PickMapTile(terrain, Camera(0), view, 320, 240), "Startup camera accepted");
        terrain.heights.pop_back();
        Check(!PickMapTile(terrain, camera, view, 320, 240), "Malformed heightmap accepted");
    }
}

int main()
{
    try
    {
        CheckMapSizesAndEdges();
        CheckMeshAgreement();
        CheckWaterAndSkirt();
        CheckInvalidInput();
        std::cout << "PASS: invalid input rejection\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
