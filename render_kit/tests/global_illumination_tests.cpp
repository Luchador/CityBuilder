#include "export.hpp"
#include "scene_file.hpp"
#include "lighting_dds.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace rk=renderkit;
namespace
{
    using Json=nlohmann::json;
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    template<class F>void Reject(F action)
    {bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,"Expected rejection");}
    bool Near(rk::Vec3 a,rk::Vec3 b,float epsilon=1e-6f){return rk::Length(a-b)<epsilon;}
    bool Equal(const std::vector<rk::Vec3>& a,const std::vector<rk::Vec3>& b)
    {return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return Near(x,y,0.00000001f);});}
    rk::Scene Fixture(bool wall)
    {
        rk::Scene s;s.materials.resize(1);s.materials[0].baseFactor={.3f,.5f,.7f,1};s.materials[0].doubleSided=true;
        auto g=std::make_shared<rk::Geometry>();
        const rk::Vec3 normal=wall?rk::Vec3{0,0,1}:rk::Vec3{0,1,0};
        const rk::Vertex a{{-1,4,wall?0.0f:-1.0f},normal},b{{1,4,wall?0.0f:-1.0f},normal},
                         c{{1,wall?6.0f:4.0f,wall?0.0f:1.0f},normal},d{{-1,wall?6.0f:4.0f,wall?0.0f:1.0f},normal};
        if(wall){g->triangles.push_back({{a,b,c},0});g->triangles.push_back({{a,c,d},0});}
        else {g->triangles.push_back({{a,c,b},0});g->triangles.push_back({{a,d,c},0});}
        g->BuildAcceleration();s.geometry=g;return s;
    }
    // Independent DDS reader: parse the public file format, not the writer's helpers.
    struct Dds {int width,height;std::vector<float> rgba;};
    Dds ReadDds(const std::filesystem::path& path)
    {
        std::ifstream f(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
        Check(bytes.size()>=148,"DDS too short");
        const auto word=[&](int offset){return std::uint32_t(bytes[offset])|std::uint32_t(bytes[offset+1])<<8|
            std::uint32_t(bytes[offset+2])<<16|std::uint32_t(bytes[offset+3])<<24;};
        Check(word(0)==0x20534444 && word(4)==124 && word(8)==0x100f && word(76)==32 && word(80)==4 && word(84)==0x30315844,
              "Invalid DDS header");
        Check(word(108)==0x1000 && word(128)==10 && word(132)==3 && word(140)==1 && word(144)==2,"Incorrect DDS format or alpha mode");
        Dds result;result.width=static_cast<int>(word(16));result.height=static_cast<int>(word(12));
        Check(word(20)==static_cast<unsigned>(result.width*8) && bytes.size()==148+static_cast<std::size_t>(result.width)*result.height*8,"DDS payload/row size");
        for(std::size_t i=148;i<bytes.size();i+=2)
        {
            const unsigned h=bytes[i]|unsigned(bytes[i+1])<<8,exponent=(h>>10)&31,mantissa=h&1023;
            Check(!(h&32768) && exponent<31,"Nonfinite/negative DDS lighting");
            result.rgba.push_back(exponent?std::ldexp(1+mantissa/1024.0f,static_cast<int>(exponent)-15):std::ldexp(static_cast<float>(mantissa),-24));
        }
        return result;
    }
    void CheckDds(const Dds& dds,const rk::RenderImage& image,const std::vector<rk::Vec3>& layer)
    {
        Check(dds.width==image.width && dds.height==image.height,"DDS dimensions changed");
        for(std::size_t i=0;i<layer.size();++i)for(int c=0;c<4;++c)
        {
            const float alpha=image.rgba[i*4+3]/255.0f;
            const float expected=c<3?rk::Component(layer[i],c)*alpha:alpha;
            Check(std::abs(dds.rgba[i*4+c]-expected)<=std::max(6e-8f,expected*.0005f),"DDS half-float value or coverage differs");
        }
    }
    void DdsTests(const std::filesystem::path& folder)
    {
        rk::RenderImage image;image.width=4;image.height=1;image.rgba.resize(16,255);
        image.directLight={{0,1,2.5f},{65504,1.0f/3,1e-5f},{std::ldexp(1.0f,-24),std::ldexp(1.0f,-14),1.00048828125f},
                           {std::ldexp(1.0f,-25),std::ldexp(3.0f,-25),1.00146484375f}};
        rk::SaveLightingDds(folder/"half.dds",4,1,image.directLight,image.rgba);
        const auto dds=ReadDds(folder/"half.dds");CheckDds(dds,image,image.directLight);
        Check(dds.rgba[2]==2.5f && dds.rgba[4]==65504 && dds.rgba[10]==1 && dds.rgba[12]==0 &&
              dds.rgba[13]==std::ldexp(1.0f,-23) && dds.rgba[14]==1.001953125f,"HDR or half-float ties-to-even lost");
        image.rgba[7]=128;image.rgba[11]=0;
        rk::SaveLightingDds(folder/"coverage.dds",4,1,image.directLight,image.rgba);
        CheckDds(ReadDds(folder/"coverage.dds"),image,image.directLight);
        Reject([&]{rk::SaveLightingDds(folder/"bad.dds",0,1,image.directLight,image.rgba);});
        image.directLight[0].x=std::numeric_limits<float>::infinity();
        Reject([&]{rk::SaveLightingDds(folder/"bad.dds",4,1,image.directLight,image.rgba);});
    }
    void SceneTests(const std::filesystem::path& folder,rk::RenderSettings settings)
    {
        rk::SceneDocument d;d.scene=rk::LoadModel(folder/rk::NativePath("model_é.glb"));d.settings=settings;
        d.outputDirectory=folder;d.previewChannel=6;
        d.settings.globalIllumination={true,24,7,3,1.25f,true,{.1f,.2f,.3f}};
        const auto path=folder/"gi.rks";rk::SaveSceneFile(path,d);
        const auto load=rk::LoadSceneFile(path);const auto& gi=load.settings.globalIllumination;
        Check(gi.enabled && gi.samples==24 && gi.previewSamples==7 && gi.bounces==3 && gi.strength==1.25f && gi.useGroundPlane &&
              Near(gi.groundColor,{.1f,.2f,.3f}) && load.previewChannel==6,"GI scene settings lost");
        std::ifstream stream(path);const auto original=Json::parse(stream);Check(original["version"]==2,"GI scenes must require the new renderer");
        const auto copy=folder/"gi_copy.rks";rk::SaveSceneFile(copy,load);std::ifstream copyStream(copy);
        Check(Json::parse(copyStream)==original,"GI scene roundtrip changed settings");
        const auto frame=rk::FitFrame(*d.scene.geometry,d.settings);
        const auto before=rk::Render(d.scene,d.settings,frame,0),after=rk::Render(load.scene,load.settings,frame,0);
        Check(before.rgba==after.rgba && Equal(before.indirectLight,after.indirectLight),"Saved GI scene changes the bake");
        auto legacy=original;legacy["version"]=1;legacy.erase("global_illumination");legacy["preview"]["channel"]=0;
        {std::ofstream f(copy);f<<legacy;}
        Check(!rk::LoadSceneFile(copy).settings.globalIllumination.enabled,"Legacy scene enabled GI");
        for(const auto& field:{"samples","preview_samples","bounces","strength","ground_color_linear","enabled","use_ground_plane"})
        {
            auto invalid=original;invalid["global_illumination"][field]=-1;
            {std::ofstream f(copy);f<<invalid;}Reject([&]{rk::LoadSceneFile(copy);});
        }
        auto invalid=d;invalid.settings.globalIllumination.strength=std::numeric_limits<float>::quiet_NaN();
        Reject([&]{rk::SaveSceneFile(path,invalid);});std::ifstream intact(path);
        Check(Json::parse(intact)==original,"Failed GI save replaced the previous scene");
    }
}
void GlobalIlluminationTests(const std::filesystem::path& folder)
{
    rk::RenderSettings settings;settings.width=settings.height=32;settings.aaGridSize=2;
    auto& gi=settings.globalIllumination;gi.enabled=true;gi.samples=32;gi.bounces=2;gi.useGroundPlane=false;
    const auto plane=Fixture(false);const auto frame=rk::FitFrame(*plane.geometry,settings);
    for(unsigned view=0;view<4;++view)
    {
        const auto image=rk::Render(plane,settings,frame,view);
        int covered=0,partial=0;
        for(std::size_t i=0;i<image.directLight.size();++i)
        {
            const auto alpha=image.rgba[i*4+3];
            if(!alpha){Check(Near(image.directLight[i],{}) && Near(image.skyLight[i],{}),"Background has lighting");continue;}
            ++covered;if(alpha<255)++partial;
            Check(Near(image.directLight[i],rk::Vec3{.3f,.5f,.7f}*rk::Normalize(rk::DirectionalLightDirection).y),"Exposed plane direct-light analytic reference failed");
            Check(Near(image.skyLight[i],{.3f,.5f,.7f}) && Near(image.indirectLight[i],{}),"Exposed plane sky/bounce analytic reference failed");
            Check(image.albedo[i*4+3]==alpha && image.directional[i*4+3]==alpha,"GI map coverage differs");
        }
        Check(covered>50 && partial>10,"GI AA fixture lacks coverage edges");
        auto other=settings;other.ambientOcclusion={true,1,100,64,true};
        Check(rk::Render(plane,other,frame,view).rgba==image.rgba,"AO applied twice in GI mode");
    }
    const auto wall=Fixture(true);const auto wallFrame=rk::FitFrame(*wall.geometry,settings);
    gi.useGroundPlane=true;gi.groundColor={.8f,.01f,.01f};gi.samples=64;gi.bounces=1;settings.lighting.shadows=false;
    const auto red=rk::Render(wall,settings,wallFrame,0);
    auto more=settings;more.globalIllumination.bounces=3;
    const auto bounced=rk::Render(wall,more,wallFrame,0);
    int colorBleed=0;double mean=0;int count=0;
    for(std::size_t i=0;i<red.directLight.size();++i)if(red.rgba[i*4+3])
    {
        if(red.indirectLight[i].x>red.indirectLight[i].z*10)++colorBleed;
        mean+=red.indirectLight[i].x;++count;
        Check(bounced.indirectLight[i].x+1e-6f>=red.indirectLight[i].x,"Additional bounces removed energy");
    }
    Check(colorBleed>100,"Ground did not cast red bounced light onto wall");
    const double expected=.3*.8*rk::Normalize(rk::DirectionalLightDirection).y*.5;
    Check(std::abs(mean/count-expected)<.008,"One-bounce wall/ground analytic reference failed");
    const auto repeated=rk::Render(wall,settings,wallFrame,0);
    Check(Equal(red.indirectLight,repeated.indirectLight) && red.rgba==repeated.rgba,"GI bake is nondeterministic");
    auto black=settings;black.globalIllumination.groundColor={0,0,0};
    const auto blackGround=rk::Render(wall,black,wallFrame,0);
    for(auto value:blackGround.indirectLight)Check(Near(value,{}),"Black ground reflected light");
    Check(red.directional==blackGround.directional,"Ground color altered direct-light visibility");
    auto noGround=settings;noGround.globalIllumination.useGroundPlane=false;
    const auto air=rk::Render(wall,noGround,wallFrame,0);
    for(std::size_t i=3;i<red.rgba.size();i+=4)Check(red.rgba[i]==air.rgba[i],"Invisible ground altered alpha");

    auto recolored=settings;recolored.lighting.ambientColor={.1f,.25f,.7f};recolored.lighting.directionalColor={1,.3f,.1f};
    recolored.lighting.ambientIntensity=.07f;recolored.lighting.directionalIntensity=.2f;recolored.globalIllumination.strength=2;
    const auto rendered=rk::Render(wall,recolored,wallFrame,0);auto relit=red;
    rk::Relight(relit,recolored.lighting,2);
    Check(relit.rgba==rendered.rgba && Equal(red.directLight,rendered.directLight) && Equal(red.indirectLight,rendered.indirectLight) &&
          Equal(red.skyLight,rendered.skyLight),"Relighting changed baked responses or differs from full render");
    auto night=settings.lighting;night.ambientIntensity=night.directionalIntensity=0;rk::Relight(relit,night,1);
    for(std::size_t i=0;i<relit.rgba.size();i+=4)Check(!relit.rgba[i] && !relit.rgba[i+1] && !relit.rgba[i+2] && relit.rgba[i+3]==red.rgba[i+3],"Night changed coverage");
    const auto display=rk::LightingLayerPreview(red,red.indirectLight);
    for(std::size_t i=3;i<display.size();i+=4)Check(display[i]==red.rgba[i],"Layer preview alpha mismatch");

    // Nearest backface hit must be used for paths, not the first triangle visited.
    auto stack=Fixture(true);auto geometry=std::make_shared<rk::Geometry>();
    for(float z:{3.0f,1.0f,2.0f})
    {
        rk::Vertex a{{-1,-1,z},{0,0,1}},b{{1,-1,z},{0,0,1}},c{{0,1,z},{0,0,1}};
        geometry->triangles.push_back({{a,b,c},0});
    }
    geometry->BuildAcceleration();stack.geometry=geometry;stack.materials[0].doubleSided=false;
    Check(!rk::Trace(stack,{0,0,0},{0,0,1},10).found,"Primary ray stopped culling single-sided backs");
    const auto hit=rk::Trace(stack,{0,0,0},{0,0,1},10,false,std::numeric_limits<std::uint32_t>::max(),true);
    Check(hit.found && hit.distance==1,"Diffuse path did not use nearest two-sided hit");

    // A fully discarded material cannot block secondary rays or create camera coverage.
    auto cutout=wall;cutout.materials[0].alphaMode=rk::AlphaMode::Cutout;
    auto mask=std::make_shared<rk::Image>();mask->width=mask->height=1;mask->rgba={255,255,255,0};
    cutout.materials[0].baseColor=mask;
    auto holes=rk::Render(cutout,settings,wallFrame,0);
    Check(std::all_of(holes.rgba.begin(),holes.rgba.end(),[](auto b){return b==0;}),"GI filled cutout holes");
    stack.materials[0].alphaMode=rk::AlphaMode::Cutout;stack.materials[0].opacity=mask;stack.materials[0].opacityChannel=rk::OpacityChannel::Alpha;
    Check(!rk::Trace(stack,{0,0,0},{0,0,1},10,false,std::numeric_limits<std::uint32_t>::max(),true).found,"Secondary rays ignored opacity mask");

    const auto output=rk::ExportViews(wall,settings,folder,"gi",true);
    std::ifstream stream(output/"sprites.json");const auto metadata=Json::parse(stream);
    Check(metadata["version"]==2 && metadata["global_illumination"]["sun_moon_shared_direction"]==true && metadata["lighting_divisor"]==1,
          "GI metadata omitted lighting model");
    for(unsigned view=0;view<4;++view)
    {
        const auto image=rk::Render(wall,settings,wallFrame,view);
        const auto name="gi_view"+std::to_string(view);
        Check(rk::LoadPng(output/(name+".png"))->rgba==image.rgba,"GI PNG export differs from preview");
        const auto direct=ReadDds(output/(name+"_direct.dds")),indirect=ReadDds(output/(name+"_indirect.dds")),sky=ReadDds(output/(name+"_sky.dds"));
        CheckDds(direct,image,image.directLight);CheckDds(indirect,image,image.indirectLight);CheckDds(sky,image,image.skyLight);
        for(std::size_t i=0;i<image.rgba.size();i+=4)for(int c=0;c<3;++c)
        {
            const float linear=(direct.rgba[i+c]+indirect.rgba[i+c])*settings.lighting.directionalIntensity+sky.rgba[i+c]*settings.lighting.ambientIntensity;
            const float coverage=image.rgba[i+3]/255.0f;
            const int byte=static_cast<int>(std::lround(std::clamp(rk::LinearToSrgb(coverage>0?linear/coverage:0),0.0f,1.0f)*255));
            Check(std::abs(byte-image.rgba[i+c])<=1,"Exported GI maps do not reconstruct the combined sprite");
        }
    }
    const auto combined=rk::ExportViews(wall,settings,folder,"combined",false);
    Check(std::distance(std::filesystem::directory_iterator(combined),std::filesystem::directory_iterator{})==5,"Combined-only export wrote layers");
    DdsTests(folder);SceneTests(folder,settings);
    std::stop_source stop;std::jthread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));stop.request_stop();});
    auto slow=settings;slow.width=slow.height=256;slow.globalIllumination.samples=1024;
    Reject([&]{rk::ExportViews(wall,slow,folder,"cancel_gi",true,stop.get_token());});
    Check(!std::filesystem::exists(folder/"cancel_gi_001"),"Cancelled GI export left partial files");
    std::cout<<"PASS: GI analytic lighting, color bleed, bounces, nearest hits, opacity, alpha, deterministic relighting, HDR DDS, four-view reconstruction, RKS and cancellation\n";
}
