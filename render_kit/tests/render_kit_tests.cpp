#include "export.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace rk=renderkit;
void FbxTests(const std::filesystem::path& folder);
void SceneFileTests(const std::filesystem::path& folder);
void AmbientOcclusionTests(const std::filesystem::path& folder);
void AntialiasingTests(const std::filesystem::path& folder);
void GlobalIlluminationTests(const std::filesystem::path& folder);
namespace
{
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    template<class Action>void Reject(Action action)
    {
        bool failed=false;try{action();}catch(const std::exception&){failed=true;}
        Check(failed,"Invalid input should have been rejected");
    }
    void Word(std::vector<std::uint8_t>& bytes,std::uint32_t word)
    {for(int i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(word>>(i*8)));}
    void FixtureGlb(const std::filesystem::path& path,const std::filesystem::path& png)
    {
        const float values[]={-1,-1,0, 1,-1,1, 0,1,.5f,
                              -.4472136f,0,.8944272f, -.4472136f,0,.8944272f, -.4472136f,0,.8944272f,
                              0,0, 1,0, .5f,1};
        std::vector<std::uint8_t> binary(sizeof(values));std::memcpy(binary.data(),values,sizeof(values));
        std::ifstream file(png,std::ios::binary);std::vector<std::uint8_t> pixels((std::istreambuf_iterator<char>(file)),{});
        const auto imageOffset=binary.size();binary.insert(binary.end(),pixels.begin(),pixels.end());
        const auto bufferSize=binary.size();while(binary.size()%4)binary.push_back(0);
        std::string json=R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]},{"nodes":[2]}],"nodes":[{"translation":[7,2,-3],"children":[1]},{"mesh":0,"scale":[-2,3,0.5]},{"mesh":0,"translation":[1000,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"material":0}]}],"materials":[{"name":"Test cutout","alphaMode":"MASK","alphaCutoff":0.4,"doubleSided":true,"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"textures":[{"source":0}],"images":[{"bufferView":3,"mimeType":"image/png"}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,1]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":)";
        json+=std::to_string(imageOffset)+",\"byteLength\":"+std::to_string(pixels.size())+"}],\"buffers\":[{\"byteLength\":"+std::to_string(bufferSize)+"}]}";
        while(json.size()%4)json+=' ';
        std::vector<std::uint8_t> bytes;Word(bytes,0x46546c67);Word(bytes,2);Word(bytes,static_cast<std::uint32_t>(28+json.size()+binary.size()));
        Word(bytes,static_cast<std::uint32_t>(json.size()));Word(bytes,0x4e4f534a);bytes.insert(bytes.end(),json.begin(),json.end());
        Word(bytes,static_cast<std::uint32_t>(binary.size()));Word(bytes,0x004e4942);bytes.insert(bytes.end(),binary.begin(),binary.end());
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    rk::Vertex Vertex(rk::Vec3 p,rk::Vec3 n={0,0,1}){return {p,n,{{{.25f,.25f},{.75f,.75f}}},{}};}
    void Quad(rk::Geometry& geometry,float z,std::uint32_t material)
    {
        const auto a=Vertex({-1,-1,z}),b=Vertex({1,-1,z}),c=Vertex({1,1,z}),d=Vertex({-1,1,z});
        geometry.triangles.push_back({{a,b,c},material});geometry.triangles.push_back({{a,c,d},material});
    }
    rk::Scene CutoutScene()
    {
        rk::Scene scene;auto geometry=std::make_shared<rk::Geometry>();
        for(int i=0;i<20;++i)Quad(*geometry,static_cast<float>(-i),i==19?1u:0u);
        geometry->BuildAcceleration();scene.geometry=std::move(geometry);scene.materials.resize(2);
        scene.materials[0].alphaMode=rk::AlphaMode::Cutout;scene.materials[0].doubleSided=true;
        auto image=std::make_shared<rk::Image>();image->width=image->height=2;
        image->rgba={0,0,0,255, 255,255,255,0, 255,255,255,0, 255,255,255,0};
        scene.materials[0].opacity=image;return scene;
    }
    rk::Scene BoxScene()
    {
        rk::Scene scene;auto geometry=std::make_shared<rk::Geometry>();
        const rk::Vec3 points[]={ {-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1}, {-1,2,-1},{1,2,-1},{1,2,1},{-1,2,1} };
        const int faces[][4]={{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0},{4,7,6,5},{0,1,2,3}};
        for(const auto& face:faces)
        {
            const auto n=rk::Normalize(rk::Cross(points[face[1]]-points[face[0]],points[face[2]]-points[face[0]]));
            auto a=Vertex(points[face[0]],n),b=Vertex(points[face[1]],n),c=Vertex(points[face[2]],n),d=Vertex(points[face[3]],n);
            geometry->triangles.push_back({{a,b,c},0});geometry->triangles.push_back({{a,c,d},0});
        }
        geometry->BuildAcceleration();scene.geometry=geometry;scene.materials.resize(1);scene.materials[0].baseFactor={.3f,.4f,.2f,1};
        return scene;
    }
    void ImportTests(const std::filesystem::path& folder)
    {
        const std::vector<std::uint8_t> pixels={255,0,0,255,0,255,0,0,0,0,255,255,255,255,255,255};
        const auto png=folder/rk::NativePath("texture_é.png");rk::SavePng(png,2,2,pixels);
        const auto image=rk::LoadPng(png);Check(image->rgba==pixels,"PNG roundtrip changed RGBA");
        const auto glb=folder/rk::NativePath("model_é.glb");FixtureGlb(glb,png);
        auto scene=rk::LoadModel(glb);Check(scene.geometry->triangles.size()==1,"Inactive scene imported");
        Check(scene.geometry->bounds.min.x==5 && scene.geometry->bounds.max.x==9,"Hierarchy/negative scale transform incorrect");
        Check(scene.materials.size()==1 && scene.materials[0].alphaMode==rk::AlphaMode::Cutout && scene.materials[0].cutoff==.4f,"Material mode lost");
        Check(scene.materials[0].baseColor && scene.materials[0].baseColor->rgba==pixels,"Embedded PNG missing");
        const auto& vertices=scene.geometry->triangles[0].vertices;
        const auto face=rk::Normalize(rk::Cross(vertices[1].position-vertices[0].position,vertices[2].position-vertices[0].position));
        Check(rk::Dot(face,vertices[0].normal)>.9999f,"Mirrored winding/inverse transpose normal incorrect");
        Check(std::abs(vertices[0].normal.x-.1240347f)<.0001f,"Nonuniform-scale normal incorrect");
        Reject([&]{rk::LoadGlb(png);});
        std::cout<<"PASS: GLB transforms, negative scale, active scene, PNG alpha, Unicode paths\n";
    }
    void RayTests()
    {
        auto scene=CutoutScene();
        auto hit=rk::Trace(scene,{.2f,.1f,2},{0,0,-1},100);
        Check(hit.found && hit.distance==21,"Cutout must reveal geometry beyond many transparent layers");
        scene.materials[0].opacityChannel=rk::OpacityChannel::Alpha;
        hit=rk::Trace(scene,{.2f,.1f,2},{0,0,-1},100);Check(hit.found && hit.distance==2,"Opacity alpha channel not used");
        scene.materials[0].opacityChannel=rk::OpacityChannel::Luminance;
        scene.materials[0].alphaMode=rk::AlphaMode::Opaque;
        hit=rk::Trace(scene,{.2f,.1f,2},{0,0,-1},100);Check(hit.found && hit.distance==2,"Opaque mode applied alpha test");
        scene.materials[0].alphaMode=rk::AlphaMode::Cutout;scene.materials[0].opacity.reset();scene.materials[0].baseFactor.a=.25f;
        scene.materials[0].cutoff=.3f;Check(rk::Trace(scene,{.2f,.1f,2},{0,0,-1},100).distance==21,"Cutoff above opacity must reject");
        scene.materials[0].cutoff=.2f;Check(rk::Trace(scene,{.2f,.1f,2},{0,0,-1},100).distance==2,"Cutoff below opacity must accept");
        scene.materials[0].cutoff=.3f;Check(!rk::Trace(scene,{.2f,.1f,2},{0,0,-1},10,true).found,"Cutout blocked a shadow ray");
        Check(!rk::Trace(scene,{10,10,2},{0,0,-1},100).found,"Background ray should miss");
        std::cout<<"PASS: nearest accepted hit, many cutout layers, opacity channels/cutoff, shadow rays\n";
    }
    void RenderTests(const std::filesystem::path& folder)
    {
        auto scene=BoxScene();rk::RenderSettings settings;settings.width=96;settings.height=128;
        const auto frame=rk::FitFrame(*scene.geometry,settings);
        for(unsigned view=0;view<4;++view)
        {
            const auto basis=rk::ViewBasis(view);
            Check(std::abs(rk::Dot(basis.right,basis.up))<.00001f && std::abs(rk::Length(basis.towardCamera)-1)<.00001f,"Camera basis invalid");
            for(const auto& t:scene.geometry->triangles)for(const auto& vertex:t.vertices)
            {
                const auto p=vertex.position-frame.center;
                const float x=settings.width*.5f+rk::Dot(p,basis.right)*frame.pixelsPerUnit;
                const float y=settings.height*.5f-rk::Dot(p,basis.up)*frame.pixelsPerUnit;
                Check(x>=settings.width*settings.padding-.01f && x<=settings.width*(1-settings.padding)+.01f &&
                      y>=settings.height*settings.padding-.01f && y<=settings.height*(1-settings.padding)+.01f,"Auto fit clips a rotation");
            }
            auto image=rk::Render(scene,settings,frame,view);std::size_t visible=0,empty=0;
            for(std::size_t i=0;i<image.rgba.size();i+=4)
            {
                const auto alpha=image.rgba[i+3];Check(alpha==0 || alpha==255,"Fractional sprite alpha");
                if(!alpha){++empty;continue;}++visible;
                Check(image.albedo[i+3]==255 && image.directional[i+3]==255,"Layer alpha differs");
                for(int c=0;c<3;++c)
                {
                    const float reconstructed=image.albedo[i+c]*(settings.lighting.ambientIntensity+settings.lighting.directionalIntensity*image.directional[i]/255.0f)/rk::LightingDivisor;
                    Check(std::abs(std::clamp(reconstructed,0.0f,255.0f)-image.rgba[i+c])<=2,"Day/night layers fail to reconstruct lit sprite");
                }
            }
            Check(visible>1000 && empty>1000,"Expected both model and transparent background");
            rk::SavePng(folder/("test_view"+std::to_string(view)+".png"),image.width,image.height,image.rgba);
            auto dark=settings;dark.lighting.ambientIntensity=dark.lighting.directionalIntensity=0;
            const auto night=rk::Render(scene,dark,frame,view);
            Check(image.directional==night.directional,"Light intensity changed reusable sun mask");
            for(std::size_t i=0;i<night.rgba.size();i+=4)
                Check(night.rgba[i]==0 && night.rgba[i+1]==0 && night.rgba[i+2]==0 && night.rgba[i+3]==image.rgba[i+3],"Night changed silhouette");
        }
        const auto first=rk::ExportViews(scene,settings,folder,"building",true);
        const auto second=rk::ExportViews(scene,settings,folder,"building",false);
        Check(first!=second && std::filesystem::exists(first/"building_view3_sun.png") && std::filesystem::exists(first/"sprites.json"),"Batch export missing layers/metadata");
        for(int view=0;view<4;++view)
        {
            const auto expected=rk::Render(scene,settings,frame,static_cast<unsigned>(view));
            const auto loaded=rk::LoadPng(first/("building_view"+std::to_string(view)+".png"));
            Check(loaded->rgba==expected.rgba,"Saved export differs from renderer");
        }
        std::stop_source cancelled;cancelled.request_stop();
        Reject([&]{rk::ExportViews(scene,settings,folder,"cancelled",true,cancelled.get_token());});
        Check(!std::filesystem::exists(folder/"cancelled_001"),"Cancelled export left partial files");
        Reject([&]{rk::ExportViews(scene,settings,folder,"../bad",false);});
        auto invalid=settings;invalid.width=8192;Reject([&]{rk::FitFrame(*scene.geometry,invalid);});
        std::cout<<"PASS: four-angle framing, binary alpha, lighting layers, PNG export, cancellation cleanup\n";
    }
}
int main()
{
    const auto folder=std::filesystem::temp_directory_path()/("render-kit-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        if(!SDL_Init(0))throw std::runtime_error(SDL_GetError());
        std::filesystem::create_directory(folder);
        ImportTests(folder);FbxTests(folder);SceneFileTests(folder);RayTests();RenderTests(folder);AmbientOcclusionTests(folder);AntialiasingTests(folder);GlobalIlluminationTests(folder);
        std::filesystem::remove_all(folder);SDL_Quit();return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr<<e.what()<<"\nTest files: "<<rk::Utf8Path(folder)<<'\n';SDL_Quit();return 1;
    }
}
