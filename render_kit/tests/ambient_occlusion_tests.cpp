#include "export.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace rk=renderkit;
namespace
{
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    rk::Scene Fixture(bool overhang)
    {
        rk::Scene scene;auto geometry=std::make_shared<rk::Geometry>();scene.materials.resize(2);
        scene.materials[0].baseFactor={.3f,.5f,.7f,1};
        const auto quad=[&](float y,float extent,std::uint32_t material)
        {
            const rk::Vertex a{{-extent,y,-extent},{0,1,0}},b{{-extent,y,extent},{0,1,0}},
                             c{{extent,y,extent},{0,1,0}},d{{extent,y,-extent},{0,1,0}};
            geometry->triangles.push_back({{a,b,c},material});geometry->triangles.push_back({{a,c,d},material});
        };
        quad(0,2,0);if(overhang)quad(.75f,.8f,1);
        geometry->BuildAcceleration();scene.geometry=geometry;return scene;
    }
    bool Same(const rk::RenderImage& a,const rk::RenderImage& b)
    {return a.rgba==b.rgba && a.albedo==b.albedo && a.directional==b.directional;}
}

void AmbientOcclusionTests(const std::filesystem::path& folder)
{
    rk::RenderSettings settings;settings.width=settings.height=80;
    settings.lighting.ambientIntensity=.4f;settings.lighting.directionalIntensity=.35f;
    auto ao=settings;ao.ambientOcclusion={true,1,3,64};
    const auto scene=Fixture(true);const auto frame=rk::FitFrame(*scene.geometry,settings);
    const auto plane=Fixture(false);const auto planeFrame=rk::FitFrame(*plane.geometry,settings);
    for(unsigned view=0;view<4;++view)
    {
        Check(Same(rk::Render(plane,settings,planeFrame,view),rk::Render(plane,ao,planeFrame,view)),
              "Exposed plane self-occludes");
        const auto original=rk::Render(scene,settings,frame,view);
        const auto shaded=rk::Render(scene,ao,frame,view);
        Check(Same(shaded,rk::Render(scene,ao,frame,view)),"AO changes between identical renders");
        auto zero=ao;zero.ambientOcclusion.intensity=0;
        Check(Same(original,rk::Render(scene,zero,frame,view)),"Zero intensity must exactly bypass AO");
        auto shortRadius=ao;shortRadius.ambientOcclusion.radius=.1f;
        Check(Same(original,rk::Render(scene,shortRadius,frame,view)),"AO sees geometry outside its radius");
        auto mediumRadius=ao;mediumRadius.ambientOcclusion.radius=1.1f;
        const auto medium=rk::Render(scene,mediumRadius,frame,view);
        auto half=ao;half.ambientOcclusion.intensity=.5f;
        const auto halfImage=rk::Render(scene,half,frame,view);
        Check(original.directional==shaded.directional,"AO changed the reusable sun mask");
        int darkened=0,expanded=0;
        for(std::size_t i=0;i<shaded.rgba.size();i+=4)
        {
            Check(shaded.rgba[i+3]==original.rgba[i+3] && shaded.albedo[i+3]==original.albedo[i+3],"AO changed binary alpha");
            for(int c=0;c<3;++c)
            {
                Check(shaded.albedo[i+c]<=halfImage.albedo[i+c] && halfImage.albedo[i+c]<=original.albedo[i+c],"AO intensity is not monotonic");
                Check(shaded.albedo[i+c]<=medium.albedo[i+c],"A larger radius reduced occlusion");
                const float reconstructed=shaded.albedo[i+c]*(ao.lighting.ambientIntensity+
                    ao.lighting.directionalIntensity*shaded.directional[i]/255.0f)/rk::LightingDivisor;
                Check(std::abs(std::clamp(reconstructed,0.0f,255.0f)-shaded.rgba[i+c])<=2,"Baked AO layers do not reconstruct the lit sprite");
                if(shaded.albedo[i+c]+3<original.albedo[i+c])
                {
                    ++darkened;
                    // Intensity mixes in LINEAR color, not gamma-encoded RGB.
                    const float expected=.5f*(rk::SrgbToLinear(original.albedo[i+c]/255.0f)+rk::SrgbToLinear(shaded.albedo[i+c]/255.0f));
                    Check(std::abs(rk::LinearToSrgb(expected)*255-halfImage.albedo[i+c])<2,"AO was not baked in linear color");
                }
                if(shaded.albedo[i+c]+3<medium.albedo[i+c])++expanded;
            }
        }
        Check(darkened>100 && expanded>100,"Overhang should occlude the floor and respond to radius");
    }

    // The underside of the single-sided card above must occlude, but its cutout
    // holes must not, whether alpha comes from the base PNG or a separate mask.
    auto cutout=scene;auto& material=cutout.materials[1];material.alphaMode=rk::AlphaMode::Cutout;
    auto texture=std::make_shared<rk::Image>();texture->width=texture->height=1;texture->rgba={255,255,255,64};
    material.baseColor=texture;
    Check(Same(rk::Render(cutout,settings,frame,0),rk::Render(cutout,ao,frame,0)),"PNG alpha holes block AO rays");
    material.cutoff=.2f;
    Check(Same(rk::Render(scene,ao,frame,0),rk::Render(cutout,ao,frame,0)),"AO ignored an adjusted cutoff");
    material.cutoff=.5f;material.useBaseColorAlpha=false;material.opacity=texture;material.opacityChannel=rk::OpacityChannel::Alpha;
    Check(Same(rk::Render(cutout,settings,frame,0),rk::Render(cutout,ao,frame,0)),"Separate opacity holes block AO rays");
    material.invertOpacity=true;
    Check(Same(rk::Render(scene,ao,frame,0),rk::Render(cutout,ao,frame,0)),"AO ignored inverted opacity");

    // Smooth normals must not turn rays into false occlusion on an exposed plane.
    auto smooth=plane;auto geometry=std::make_shared<rk::Geometry>(*plane.geometry);
    for(auto& triangle:geometry->triangles)for(auto& vertex:triangle.vertices)vertex.normal=rk::Normalize({.8f,.2f,0});
    smooth.geometry=geometry;
    Check(Same(rk::Render(smooth,settings,planeFrame,0),rk::Render(smooth,ao,planeFrame,0)),"Smooth normals cause self-occlusion");

    const auto output=rk::ExportViews(scene,ao,folder,"ao",true);
    std::ifstream file(output/"sprites.json");const auto metadata=nlohmann::json::parse(file);
    Check(metadata["ambient_occlusion"]["enabled"]==true && metadata["ambient_occlusion"]["radius_model_units"]==3,
          "Export metadata lost AO settings");
    for(unsigned view=0;view<4;++view)
    {
        const auto expected=rk::Render(scene,ao,frame,view);
        const auto name="ao_view"+std::to_string(view);
        Check(rk::LoadPng(output/(name+"_albedo.png"))->rgba==expected.albedo,"Export lost baked AO");
        Check(rk::LoadPng(output/(name+".png"))->rgba==expected.rgba,"Lit export differs from AO preview");
    }
    Check(std::distance(std::filesystem::directory_iterator(output),std::filesystem::directory_iterator{})==13,
          "AO must not add a separate output sprite");
    std::cout<<"PASS: AO intensity/radius, deterministic four-view baking, linear color, alpha/cutouts, sun preservation and export\n";
}
