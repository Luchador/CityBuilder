#include "scene_file.hpp"
#include "export.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace rk=renderkit;
namespace
{
    using Json=nlohmann::json;
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    template<class Action>void Reject(Action action,const char* expected)
    {
        try{action();}catch(const std::exception& e)
        {Check(std::string(e.what()).find(expected)!=std::string::npos,"Scene failure did not explain the problem");return;}
        throw std::runtime_error("Invalid scene was accepted");
    }
    Json Read(const std::filesystem::path& path){std::ifstream file(path);return Json::parse(file);}
    void Write(const std::filesystem::path& path,const Json& json){std::ofstream file(path);file<<json.dump(2);}
    void CompareRender(const rk::SceneDocument& before,const rk::SceneDocument& after)
    {
        for(unsigned view=0;view<4;++view)
        {
            const auto a=rk::Render(before.scene,before.settings,rk::FitFrame(*before.scene.geometry,before.settings),view);
            const auto b=rk::Render(after.scene,after.settings,rk::FitFrame(*after.scene.geometry,after.settings),view);
            Check(a.rgba==b.rgba && a.albedo==b.albedo && a.directional==b.directional,"Saved scene changed rendered pixels");
        }
    }
}

void SceneFileTests(const std::filesystem::path& folder)
{
    const auto scenePath=folder/rk::NativePath("saved scene_é.rks");
    const auto secondPath=folder/"saved_again.rks";
    rk::SceneDocument document;
    document.scene=rk::LoadModel(folder/rk::NativePath("model_é.glb"));
    document.outputDirectory=folder;document.outputPrefix="test_scene";document.exportLayers=false;
    document.settings.width=80;document.settings.height=96;document.settings.padding=.13f;
    document.settings.autoFit=false;document.settings.pixelsPerUnit=8.5f;
    document.settings.lighting.ambientColor={.5f,.7f,.9f};document.settings.lighting.ambientIntensity=.35f;
    document.settings.lighting.directionalColor={1,.8f,.6f};document.settings.lighting.directionalIntensity=1.7f;
    document.settings.lighting.shadows=false;document.view=3;document.previewChannel=2;
    document.settings.ambientOcclusion={true,.65f,2.5f,48};
    document.settings.ambientOcclusion.useGroundPlane=true;
    auto& material=document.scene.materials[0];
    material.baseFactor={.7f,.8f,.4f,.9f};material.cutoff=.25f;material.doubleSided=false;
    material.uvScale={.8f,1.2f};material.uvOffset={.2f,-.3f};material.uvRotation=.33f;
    material.texcoord=1;material.wrapU=rk::Wrap::Mirror;material.wrapV=rk::Wrap::Clamp;
    material.opacity=rk::LoadPng(folder/rk::NativePath("texture_é.png"));
    material.useBaseColorAlpha=false;material.invertOpacity=true;material.opacityStrength=.6f;
    material.opacityChannel=rk::OpacityChannel::Red;material.separateOpacityUv=true;
    material.opacityWrapU=rk::Wrap::Clamp;material.opacityWrapV=rk::Wrap::Mirror;
    rk::SaveSceneFile(scenePath,document);
    const auto saved=Read(scenePath);
    Check(saved["materials"][0]["base_color"]["kind"]=="model","Embedded PNG must reference the model");
    Check(saved["materials"][0]["opacity"]["kind"]=="file","Assigned PNG must have a file reference");
    Check(!rk::NativePath(saved["model"].get<std::string>()).is_absolute(),"Scene asset paths should be relative when possible");
    const auto loaded=rk::LoadSceneFile(scenePath);
    CompareRender(document,loaded);
    Check(loaded.view==3 && loaded.previewChannel==2 && loaded.outputPrefix=="test_scene" && !loaded.exportLayers,"Editor/output settings were not restored");
    rk::SaveSceneFile(secondPath,loaded);
    Check(Read(secondPath)==saved,"Scene roundtrip lost a setting");
    Check(loaded.settings.ambientOcclusion.enabled && loaded.settings.ambientOcclusion.intensity==.65f &&
          loaded.settings.ambientOcclusion.radius==2.5f && loaded.settings.ambientOcclusion.samples==48 &&
          loaded.settings.ambientOcclusion.useGroundPlane,"AO settings were not restored");
    auto beforeGround=saved;beforeGround["ambient_occlusion"].erase("use_ground_plane");Write(secondPath,beforeGround);
    const auto oldAoScene=rk::LoadSceneFile(secondPath);
    Check(oldAoScene.settings.ambientOcclusion.enabled && !oldAoScene.settings.ambientOcclusion.useGroundPlane,
          "Older AO scenes must load without a ground plane");
    auto noGround=document;noGround.settings.ambientOcclusion.useGroundPlane=false;CompareRender(noGround,oldAoScene);
    rk::SaveSceneFile(secondPath,noGround);
    Check(!rk::LoadSceneFile(secondPath).settings.ambientOcclusion.useGroundPlane,"Disabled ground plane did not roundtrip");
    auto legacy=saved;legacy.erase("ambient_occlusion");Write(secondPath,legacy);
    const auto oldScene=rk::LoadSceneFile(secondPath);
    Check(!oldScene.settings.ambientOcclusion.enabled && !oldScene.settings.ambientOcclusion.useGroundPlane,
          "Pre-AO .rks files must retain their appearance");
    auto original=document;original.settings.ambientOcclusion.enabled=false;CompareRender(original,oldScene);

    // A user-cleared texture must not be restored from the original model.
    material.baseColor.reset();material.opacity.reset();material.alphaMode=rk::AlphaMode::Opaque;
    rk::SaveSceneFile(scenePath,document);
    const auto cleared=rk::LoadSceneFile(scenePath);
    Check(!cleared.scene.materials[0].baseColor && !cleared.scene.materials[0].opacity,"Cleared texture reappeared after loading");
    CompareRender(document,cleared);

    // PNG replacement, alpha conventions, independent UVs, and both FBX material slots.
    document.scene=rk::LoadModel(folder/rk::NativePath("tree_é.FBX"));document.selectedMaterial=1;
    document.scene.materials[0].baseColor=rk::LoadPng(folder/rk::NativePath("texture_é.png"));
    document.scene.materials[0].opacity=rk::LoadPng(folder/"mask.png");
    document.scene.materials[0].invertOpacity=true;document.scene.materials[0].useBaseColorAlpha=false;
    document.scene.materials[0].separateOpacityUv=true;document.scene.materials[0].cutoff=.32f;
    document.scene.materials[1].baseFactor={.25f,.55f,.8f,1};
    rk::SaveSceneFile(scenePath,document);const auto fbxLoaded=rk::LoadSceneFile(scenePath);
    CompareRender(document,fbxLoaded);rk::SaveSceneFile(secondPath,fbxLoaded);
    Check(Read(scenePath)==Read(secondPath),"FBX scene roundtrip lost a setting");

    // Saving under another directory recomputes paths without moving or copying assets.
    const auto elsewhere=folder/"scenes";std::filesystem::create_directory(elsewhere);
    rk::SaveSceneFile(elsewhere/"other.rks",fbxLoaded);
    Check(Read(elsewhere/"other.rks")["model"].get<std::string>().starts_with("../"),"Save As did not rebase relative paths");
    CompareRender(document,rk::LoadSceneFile(elsewhere/"other.rks"));

    const auto valid=Read(scenePath);
    const auto invalidPath=folder/"invalid.rks";
    for(const auto& field:{"intensity","radius","samples"})
    {
        auto badAo=valid;badAo["ambient_occlusion"][field]=-1;Write(invalidPath,badAo);
        Reject([&]{rk::LoadSceneFile(invalidPath);},field==std::string("samples")?"range":"ambient occlusion");
    }
    auto badAo=valid;badAo["ambient_occlusion"]["radius"]=nullptr;Write(invalidPath,badAo);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"number");
    badAo=valid;badAo["ambient_occlusion"]["use_ground_plane"]="yes";Write(invalidPath,badAo);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"boolean");
    auto invalid=valid;invalid["version"]=99;Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"version");
    invalid=valid;invalid["render"]["width"]=1.5;Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"whole number");
    invalid=valid;invalid["materials"][0]["texcoord"]=4;Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"range");
    invalid=valid;invalid["materials"][0]["name"]="Changed";Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"changed name/order");
    invalid=valid;invalid["materials"].erase(1);invalid["preview"]["material"]=0;Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"material list has changed");
    invalid=valid;invalid["model"]="missing.glb";Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"missing.glb");
    invalid=valid;invalid["materials"][0]["base_color"]={{"kind","file"},{"path","missing.png"}};Write(invalidPath,invalid);
    Reject([&]{rk::LoadSceneFile(invalidPath);},"missing.png");
    {std::ofstream file(invalidPath);file<<"{malformed";}
    Reject([&]{rk::LoadSceneFile(invalidPath);},"Invalid Render Kit scene");
    std::stop_source cancelled;cancelled.request_stop();
    Reject([&]{rk::LoadSceneFile(scenePath,cancelled.get_token());},"cancelled");

    // A failed save must leave the previous complete file intact.
    auto bad=document;bad.settings.width=0;
    Reject([&]{rk::SaveSceneFile(scenePath,bad);},"range");
    Check(Read(scenePath)==valid,"Failed save overwrote the existing scene");
    bad=document;bad.scene.materials[0].cutoff=std::numeric_limits<float>::quiet_NaN();
    Reject([&]{rk::SaveSceneFile(scenePath,bad);},"number");
    Check(Read(scenePath)==valid,"Nonfinite save overwrote existing scene");
    bad=document;bad.settings.ambientOcclusion.radius=std::numeric_limits<float>::quiet_NaN();
    Reject([&]{rk::SaveSceneFile(scenePath,bad);},"number");
    Check(Read(scenePath)==valid,"Nonfinite AO save overwrote existing scene");
    const auto blocked=folder/"blocked.rks";std::filesystem::create_directory(blocked);
    Reject([&]{rk::SaveSceneFile(blocked,document);},"replace scene");
    for(const auto& entry:std::filesystem::directory_iterator(folder))
        Check(!rk::Utf8Path(entry.path().filename()).starts_with(".render-kit-save-"),"Failed save left temporary files");

    // Move the project bundle: model, textures and .rks paths should still resolve.
    const auto bundle=folder/"bundle";std::filesystem::create_directory(bundle);
    std::filesystem::copy_file(folder/rk::NativePath("model_é.glb"),bundle/"model.glb");
    std::filesystem::copy_file(folder/rk::NativePath("texture_é.png"),bundle/"texture.png");
    rk::SceneDocument portable;portable.scene=rk::LoadModel(bundle/"model.glb");
    portable.scene.materials[0].baseColor=rk::LoadPng(bundle/"texture.png");portable.outputDirectory=bundle;
    portable.settings.width=portable.settings.height=64;
    rk::SaveSceneFile(bundle/"portable.rks",portable);
    const auto moved=folder/"moved_bundle";std::filesystem::rename(bundle,moved);
    const auto relocated=rk::LoadSceneFile(moved/"portable.rks");
    CompareRender(portable,relocated);
    Check(relocated.scene.materials[0].baseColor->sourcePath==moved/"texture.png","Moved texture path was not resolved relative to .rks");
    const auto result=rk::ExportViews(relocated.scene,relocated.settings,relocated.outputDirectory,relocated.outputPrefix,relocated.exportLayers);
    Check(std::filesystem::exists(result/"sprite_view3_sun.png"),"Loaded scene cannot export sprites");
    std::cout<<"PASS: RKS GLB/FBX roundtrips, all settings, embedded/replaced/cleared PNGs, relocation, validation and safe saves\n";
}
