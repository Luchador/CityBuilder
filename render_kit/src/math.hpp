#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace renderkit
{
    struct Vec2 { float x = 0, y = 0; };
    struct Vec3 { float x = 0, y = 0, z = 0; };
    struct Vec4 { float r = 1, g = 1, b = 1, a = 1; };
    inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
    inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
    inline Vec3 operator*(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
    inline Vec3 operator/(Vec3 a, float s) { return a * (1.0f/s); }
    inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
    inline float Length(Vec3 a) { return std::sqrt(Dot(a,a)); }
    inline Vec3 Normalize(Vec3 a) { const float n = Length(a); return n > 1e-20f ? a/n : Vec3{0,1,0}; }
    inline bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
    inline float Component(Vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
    inline float SrgbToLinear(float c) { return c <= .04045f ? c/12.92f : std::pow((c+.055f)/1.055f,2.4f); }
    inline float LinearToSrgb(float c) { c=std::max(0.0f,c); return c <= .0031308f ? c*12.92f : 1.055f*std::pow(c,1/2.4f)-.055f; }

    struct Bounds
    {
        Vec3 min{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
        Vec3 max{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
        void Include(Vec3 p)
        {
            min = {std::min(min.x,p.x),std::min(min.y,p.y),std::min(min.z,p.z)};
            max = {std::max(max.x,p.x),std::max(max.y,p.y),std::max(max.z,p.z)};
        }
        Vec3 Center() const { return (min+max)*.5f; }
    };
}
