#pragma once

class Camera
{
    public:
        Camera();

        void Zoom(float steps);
        void ResetZoom();

        float GetHalfHeight() const;

    private:
        static constexpr float DefaultHalfHeight = 120.0f;

        float halfHeight = DefaultHalfHeight;
};