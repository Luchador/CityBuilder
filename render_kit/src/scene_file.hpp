#pragma once
#include "render.hpp"

namespace renderkit
{
    // The complete editable state; geometry is reimported from the referenced model.
    struct SceneDocument
    {
        Scene scene;
        RenderSettings settings;
        std::filesystem::path outputDirectory;
        std::string outputPrefix="sprite";
        bool exportLayers=true;
        int view=0, previewChannel=0, selectedMaterial=0;
    };

    bool IsSceneFile(const std::filesystem::path& path);
    // Save writes a complete temporary file before replacing the destination.
    void SaveSceneFile(const std::filesystem::path& path,const SceneDocument& document);
    // Returns a fully loaded candidate; errors never change the caller's scene.
    SceneDocument LoadSceneFile(const std::filesystem::path& path,std::stop_token stop={});
}
