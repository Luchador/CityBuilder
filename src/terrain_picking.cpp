#include "terrain_picking.hpp"

#include "camera.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr double HitTolerance = 1.0e-8;

    struct Vector
    {
        double x, y, z;
        Vector operator+(Vector b) const { return {x + b.x, y + b.y, z + b.z}; }
        Vector operator-(Vector b) const { return {x - b.x, y - b.y, z - b.z}; }
        Vector operator*(double s) const { return {x * s, y * s, z * s}; }
    };

    double Dot(Vector a, Vector b) 
    { 
        return a.x * b.x + a.y * b.y + a.z * b.z; 
    }

    Vector Cross(Vector a, Vector b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    struct Ray
    {
        Vector origin, direction;
        double length;
    };

    std::optional<Ray> CursorRay(const Camera& camera, const PickingViewport& view, float screenX, float screenY)
    {
        if (view.width <= 0 || view.height <= 0 || !std::isfinite(screenX) || !std::isfinite(screenY) ||
            !std::isfinite(view.pixelsPerWindowX) || !std::isfinite(view.pixelsPerWindowY) ||
            view.pixelsPerWindowX <= 0 || view.pixelsPerWindowY <= 0 ||
            !std::isfinite(camera.GetHalfHeight()) || camera.GetHalfHeight() <= 0 ||
            !std::isfinite(camera.GetCenterX()) || !std::isfinite(camera.GetCenterZ()) ||
            !std::isfinite(camera.GetMapExtent()) || camera.GetMapExtent() <= 0)
        {
            return std::nullopt;
        }

        const double px = static_cast<double>(screenX) * view.pixelsPerWindowX - view.x;
        const double py = static_cast<double>(screenY) * view.pixelsPerWindowY - view.y;

        if (px < 0 || py < 0 || px >= view.width || py >= view.height) 
        {
            return std::nullopt;
        }

        // Match the float aspect passed to the shader. Window coordinates are
        // continuous: do not add half a pixel to SDL's cursor position.
        const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
        const double right = (2.0 * px / view.width - 1.0) * camera.GetHalfHeight() * aspect;
        const double up = (1.0 - 2.0 * py / view.height) * camera.GetHalfHeight();
        const auto orientation = camera.GetOrientation();
        const double c = orientation.cosYaw, s = orientation.sinYaw;
        const double sp = orientation.sinPitch, cp = orientation.cosPitch;
        const double yawLength = c * c + s * s;
        const double pitchLength = sp * sp + cp * cp;
        const double depthSpan = std::max(2048.0, 4.0 * camera.GetMapExtent());

        // Invert ProjectWorldPosition. Using the actual float coefficients
        // avoids assuming that their rounded basis vectors are exactly unit.
        const double forward = (cp * (depthSpan * 0.5) - sp * up) / pitchLength;
    
        const Vector origin{camera.GetCenterX() + (c * right + s * forward) / yawLength, (cp * up + sp * (depthSpan * 0.5)) / pitchLength, camera.GetCenterZ() + (-s * right + c * forward) / yawLength};
    
        // Parameter distance is decreasing shader towardCamera, not meters.
        const Vector direction{-s * cp / (yawLength * pitchLength), -sp / pitchLength, -c * cp / (yawLength * pitchLength)};

        return Ray{origin, direction, depthSpan}; // Depth 0 through depth 1.
    }

    bool ClipAxis(double origin, double direction, double extent, double& first, double& last)
    {
        if (direction == 0.0) 
        {
            return origin >= 0.0 && origin <= extent;
        }

        double a = -origin / direction;
        double b = (extent - origin) / direction;

        if (a > b) 
        {
            std::swap(a, b);
        }

        first = std::max(first, a);
        last = std::min(last, b);

        return first <= last;
    }

    int InitialCell(double coordinate, double direction, int count)
    {
        coordinate = std::clamp(coordinate, 0.0, static_cast<double>(count));
        int cell = static_cast<int>(std::floor(coordinate));

        if (direction < 0.0 && coordinate == cell) 
        {
            --cell;
        }

        return std::clamp(cell, 0, count - 1);
    }

    // Moller-Trumbore intersection; reject backfaces just like the renderer.
    std::optional<double> TriangleHit(const Ray& ray, Vector a, Vector b, Vector c, double first, double last)
    {
        const Vector ab = b - a, ac = c - a;
        const Vector p = Cross(ray.direction, ac);
        const double determinant = Dot(ab, p);

        if (determinant <= 1.0e-12) 
        {
            return std::nullopt;
        }

        const double inverse = 1.0 / determinant;
        const Vector fromA = ray.origin - a;
        const double u = Dot(fromA, p) * inverse;

        if (u < -HitTolerance || u > 1.0 + HitTolerance) 
        {
            return std::nullopt;
        }

        const Vector q = Cross(fromA, ab);
        const double v = Dot(ray.direction, q) * inverse;
        if (v < -HitTolerance || u + v > 1.0 + HitTolerance) 
        {
            return std::nullopt;
        }

        const double distance = Dot(ac, q) * inverse;

        if (distance < first - HitTolerance || distance > last + HitTolerance) 
        {
            return std::nullopt;
        }

        return std::clamp(distance, first, last);
    }

    struct Cell
    {
        Vector a, b, c, d;

        double Height(double x, double z) const
        {
            const double u = std::clamp(x - a.x, 0.0, 1.0);
            const double v = std::clamp(z - a.z, 0.0, 1.0);
            // Same b-c diagonal as BuildTerrain/BuildWater; NOT bilinear.

            return u + v <= 1.0 ? a.y + (b.y - a.y) * u + (c.y - a.y) * v : d.y + (c.y - d.y) * (1.0 - u) + (b.y - d.y) * (1.0 - v);
        }
    };

    Cell ReadCell(const TerrainHeightmap& terrain, int x, int z)
    {
        const auto ix = static_cast<std::uint32_t>(x), iz = static_cast<std::uint32_t>(z);

        return {{static_cast<double>(x), terrain.Height(ix, iz), static_cast<double>(z)},
                {static_cast<double>(x + 1), terrain.Height(ix + 1, iz), static_cast<double>(z)},
                {static_cast<double>(x), terrain.Height(ix, iz + 1), static_cast<double>(z + 1)},
                {static_cast<double>(x + 1), terrain.Height(ix + 1, iz + 1), static_cast<double>(z + 1)}};
    }

    double SnapEdge(double coordinate, int count)
    {
        const double rounded = std::round(coordinate);
    
        if (std::abs(coordinate - rounded) <= HitTolerance) 
        {
            coordinate = rounded;
        }

        return std::clamp(coordinate, 0.0, static_cast<double>(count));
    }

    ToolHit MakeHit(Vector point, int count, ToolHitSurface surface)
    {
        point.x = SnapEdge(point.x, count);
        point.z = SnapEdge(point.z, count);
        return {{std::min(static_cast<int>(std::floor(point.x)), count - 1),
                 std::min(static_cast<int>(std::floor(point.z)), count - 1)},
                static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z), surface};
    }
}

std::optional<ToolHit> PickMapTile(const TerrainHeightmap& terrain, const Camera& camera, const PickingViewport& viewport, float screenX, float screenY, const TerrainPickOptions& options)
{
    // Shape check is O(1). World validates the actual height values at creation;
    // never scan a million-height map on every cursor update.
    if (terrain.heights.size() != terrain.dimensions.CornerCount() || (options.skirtBottomY && !std::isfinite(*options.skirtBottomY))) return std::nullopt;

    const auto ray = CursorRay(camera, viewport, screenX, screenY);

    if (!ray) 
    {
        return std::nullopt;
    }

    const int count = static_cast<int>(terrain.dimensions.TileCount());
    double first = 0.0, last = ray->length;

    if (!ClipAxis(ray->origin.x, ray->direction.x, count, first, last) || !ClipAxis(ray->origin.z, ray->direction.z, count, first, last)) 
    {
        return std::nullopt;
    }

    const Vector entry = ray->origin + ray->direction * first;
    int x = InitialCell(entry.x, ray->direction.x, count);
    int z = InitialCell(entry.z, ray->direction.z, count);

    // The first map boundary is a camera-facing skirt wall. A ray entering
    // through rock must not select a distant hill behind that opaque wall.
    if (options.skirtBottomY && first > 0.0)
    {
        const double top = ReadCell(terrain, x, z).Height(entry.x, entry.z);

        if (entry.y >= *options.skirtBottomY && entry.y < top - HitTolerance) 
        {
            return std::nullopt;
        }
    }

    const int stepX = ray->direction.x > 0 ? 1 : -1;
    const int stepZ = ray->direction.z > 0 ? 1 : -1;
    const double deltaX = std::abs(1.0 / ray->direction.x);
    const double deltaZ = std::abs(1.0 / ray->direction.z);
    double nextX = ((stepX > 0 ? x + 1.0 : x) - ray->origin.x) / ray->direction.x;
    double nextZ = ((stepZ > 0 ? z + 1.0 : z) - ray->origin.z) / ray->direction.z;

    // Grid traversal visits each crossed cell front to back. The current four
    // diagonal camera orientations always have nonzero X/Z ray components.
    for (int visited = 0; visited < 2 * count + 4 && x >= 0 && z >= 0 && x < count && z < count; ++visited)
    {
        const double cellEnd = std::max(first, std::min({nextX, nextZ, last}));
        const Cell cell = ReadCell(terrain, x, z);
        auto nearest = TriangleHit(*ray, cell.a, cell.c, cell.b, first, cellEnd);
        const auto second = TriangleHit(*ray, cell.b, cell.c, cell.d, first, cellEnd);

        if (second && (!nearest || *second < *nearest)) 
        {
            nearest = second;
        }

        ToolHitSurface surface = ToolHitSurface::Terrain;

        if (options.includeWater)
        {
            const double water = (TerrainSeaLevel - ray->origin.y) / ray->direction.y;
    
            if (water >= first - HitTolerance && water <= cellEnd + HitTolerance && (!nearest || water < *nearest))
            {
                const Vector point = ray->origin + ray->direction * water;
                // Match waterDepth > 0 in WaterFragmentShaderSource. This
                // rejects dry portions of shoreline quads as well as dry tiles.

                if (cell.Height(point.x, point.z) < TerrainSeaLevel)
                {
                    nearest = std::clamp(water, first, cellEnd);
                    surface = ToolHitSurface::Water;
                }
            }
        }

        if (nearest)
        {
            Vector point = ray->origin + ray->direction * *nearest;
    
            if (surface == ToolHitSurface::Water) 
            {
                point.y = TerrainSeaLevel;
            }

            return MakeHit(point, count, surface);
        }

        if (cellEnd >= last) 
        {
            break;
        }

        const bool crossX = nextX <= nextZ;
        const bool crossZ = nextZ <= nextX;
        first = cellEnd;

        if (crossX) 
        { 
            x += stepX; 
            nextX += deltaX; 
        }

        if (crossZ) 
        { 
            z += stepZ; 
            nextZ += deltaZ; 
        }
    }

    return std::nullopt;
}
