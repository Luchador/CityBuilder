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

    rk::Scene WallFixture(bool realFloor)
    {
        // Ground is deliberately above Y=0 to test placement at the model's base.
        rk::Scene scene;auto geometry=std::make_shared<rk::Geometry>();scene.materials.resize(1);
        scene.materials[0].baseFactor={.3f,.5f,.7f,1};scene.materials[0].doubleSided=true;
        const rk::Vertex a{{-1,4,0},{0,0,1}},b{{1,4,0},{0,0,1}},
                         c{{1,6,0},{0,0,1}},d{{-1,6,0},{0,0,1}};
        geometry->triangles.push_back({{a,b,c},0});geometry->triangles.push_back({{a,c,d},0});
        if(realFloor)
        {
            const rk::Vertex e{{-4,4,-4},{0,1,0}},f{{-4,4,4},{0,1,0}},
                             g{{4,4,4},{0,1,0}},h{{4,4,-4},{0,1,0}};
            geometry->triangles.push_back({{e,f,g},0});geometry->triangles.push_back({{e,g,h},0});
        }
        geometry->BuildAcceleration();scene.geometry=geometry;return scene;
    }

    void GroundPlaneTests(const std::filesystem::path& folder)
    {
        const auto wall=WallFixture(false),reference=WallFixture(true);
        rk::RenderSettings settings;settings.width=settings.height=80;
        settings.lighting.ambientIntensity=.4f;settings.lighting.directionalIntensity=.35f;
        settings.ambientOcclusion={true,1,.6f,128};
        const auto frame=rk::FitFrame(*wall.geometry,settings);
        auto ground=settings;ground.ambientOcclusion.useGroundPlane=true;
        auto half=ground;half.ambientOcclusion.intensity=.5f;
        auto wider=ground;wider.ambientOcclusion.radius=1.2f;
        auto zero=ground;zero.ambientOcclusion.intensity=0;
        auto off=ground;off.ambientOcclusion.enabled=false;
        for(unsigned view=0;view<4;++view)
        {
            const auto plain=rk::Render(wall,settings,frame,view);
            const auto shaded=rk::Render(wall,ground,frame,view);
            const auto actualFloor=rk::Render(reference,settings,frame,view);
            const auto halfImage=rk::Render(wall,half,frame,view);
            const auto widerImage=rk::Render(wall,wider,frame,view);
            Check(Same(plain,rk::Render(wall,off,frame,view)) && Same(plain,rk::Render(wall,zero,frame,view)),
                  "Ground plane affected disabled or zero-intensity AO");
            Check(Same(shaded,rk::Render(wall,ground,frame,view)),"Ground AO is not deterministic");
            Check(shaded.directional==plain.directional,"Invisible ground changed the sun mask");
            int darkened=0,expanded=0,unaffected=0,transparent=0;
            for(std::size_t i=0;i<plain.rgba.size();i+=4)
            {
                Check(shaded.rgba[i+3]==plain.rgba[i+3] && shaded.albedo[i+3]==plain.albedo[i+3],
                      "Invisible ground changed the silhouette");
                if(!plain.rgba[i+3])
                {
                    ++transparent;
                    for(int c=0;c<4;++c)Check(!shaded.rgba[i+c] && !shaded.albedo[i+c] && !shaded.directional[i+c],
                        "Ground plane appeared in a transparent background pixel");
                    continue;
                }
                if(shaded.albedo[i]+3<plain.albedo[i])++darkened;
                if(widerImage.albedo[i]+3<shaded.albedo[i])++expanded;
                if(shaded.albedo[i]==plain.albedo[i])++unaffected;
                for(int c=0;c<3;++c)
                {
                    Check(std::abs(int(shaded.albedo[i+c])-int(actualFloor.albedo[i+c]))<=1,
                          "Invisible ground AO differs from real ground geometry");
                    Check(shaded.albedo[i+c]<=halfImage.albedo[i+c] && halfImage.albedo[i+c]<=plain.albedo[i+c],
                          "Ground AO intensity is not monotonic");
                    Check(widerImage.albedo[i+c]<=shaded.albedo[i+c],"Ground AO radius is not monotonic");
                }
            }
            Check(darkened>100 && expanded>100 && unaffected>100 && transparent>100,
                  "Ground contact must darken nearby surfaces and leave distant surfaces/background alone");
        }
        // Upward-facing surfaces at ground height must not self-occlude.
        const auto floor=Fixture(false);const auto floorFrame=rk::FitFrame(*floor.geometry,settings);
        Check(Same(rk::Render(floor,settings,floorFrame,0),rk::Render(floor,ground,floorFrame,0)),
              "Ground plane self-occludes a coincident upward-facing surface");
        // Completely discarded cutout pixels still miss, even with ground enabled.
        auto cutout=wall;cutout.materials[0].alphaMode=rk::AlphaMode::Cutout;cutout.materials[0].baseFactor.a=0;
        const auto holes=rk::Render(cutout,ground,frame,0);
        Check(std::all_of(holes.rgba.begin(),holes.rgba.end(),[](auto byte){return byte==0;}),
              "Ground plane filled a cutout hole");
        const auto output=rk::ExportViews(wall,ground,folder,"ground_ao",true);
        std::ifstream file(output/"sprites.json");const auto metadata=nlohmann::json::parse(file);
        Check(metadata["ambient_occlusion"]["use_ground_plane"]==true && metadata["ground_pivot"][1]==4,
              "Export metadata lost the ground-plane setting or height");
        for(unsigned view=0;view<4;++view)
        {
            const auto expected=rk::Render(wall,ground,frame,view);
            const auto name="ground_ao_view"+std::to_string(view);
            Check(rk::LoadPng(output/(name+".png"))->rgba==expected.rgba &&
                  rk::LoadPng(output/(name+"_albedo.png"))->rgba==expected.albedo &&
                  rk::LoadPng(output/(name+"_sun.png"))->rgba==expected.directional,
                  "Ground AO export differs from preview");
        }
        std::cout<<"PASS: invisible ground AO in four views, model-base height, real-plane agreement, radius/intensity, alpha/sun preservation and export\n";
    }
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
    GroundPlaneTests(folder);
}
