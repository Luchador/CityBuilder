#include "export.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace rk=renderkit;
namespace
{
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    bool Same(const rk::RenderImage& a,const rk::RenderImage& b)
    {return a.rgba==b.rgba && a.albedo==b.albedo && a.directional==b.directional;}
    rk::Scene Card(bool twoColors,bool cutout)
    {
        rk::Scene scene;auto geometry=std::make_shared<rk::Geometry>();scene.materials.resize(2);
        const rk::Vertex a{{-1,4,0},{0,0,1},{{{0,0},{0,0}}}},b{{1,4,0},{0,0,1},{{{1,0},{1,0}}}},
                         c{{1,6,0},{0,0,1},{{{1,1},{1,1}}}},d{{-1,6,0},{0,0,1},{{{0,1},{0,1}}}};
        geometry->triangles.push_back({{a,b,c},0});geometry->triangles.push_back({{a,c,d},twoColors?1u:0u});
        geometry->BuildAcceleration();scene.geometry=geometry;
        auto mask=std::make_shared<rk::Image>();mask->width=mask->height=2;
        mask->rgba={255,255,255,255,255,255,255,0,255,255,255,0,255,255,255,255};
        for(auto& material:scene.materials)
        {
            material.doubleSided=true;material.baseFactor={.8f,.05f,.1f,1};
            if(cutout){material.alphaMode=rk::AlphaMode::Cutout;material.baseColor=mask;material.cutoff=.6f;}
        }
        scene.materials[1].baseFactor={.05f,.2f,.8f,1};
        return scene;
    }
    // Independent oracle: render a larger image with AA disabled, then reduce
    // its actual byte pixels using coverage-weighted, linear-light filtering.
    void CompareReduction(const rk::RenderImage& low,const rk::RenderImage& high,int grid)
    {
        const std::vector<std::uint8_t>* lowMaps[]{&low.rgba,&low.albedo,&low.directional};
        const std::vector<std::uint8_t>* highMaps[]{&high.rgba,&high.albedo,&high.directional};
        for(int y=0;y<low.height;++y)for(int x=0;x<low.width;++x)
        {
            const auto i=static_cast<std::size_t>(y*low.width+x)*4;
            int hits=0;double sums[3][3]{};
            for(int sy=0;sy<grid;++sy)for(int sx=0;sx<grid;++sx)
            {
                const auto j=static_cast<std::size_t>((y*grid+sy)*high.width+x*grid+sx)*4;
                if(!high.rgba[j+3])continue;
                ++hits;
                for(int m=0;m<3;++m)for(int c=0;c<3;++c)
                {
                    const float value=(*highMaps[m])[j+c]/255.0f;
                    sums[m][c]+=m==2?value:rk::SrgbToLinear(value);
                }
            }
            const auto alpha=std::lround(255.0*hits/(grid*grid));
            for(int m=0;m<3;++m)
            {
                Check((*lowMaps[m])[i+3]==alpha,"AA alpha differs from independent supersized render");
                for(int c=0;c<3;++c)
                {
                    const float average=hits?static_cast<float>(sums[m][c]/hits):0;
                    const auto expected=std::lround(255.0f*(m==2?average:rk::LinearToSrgb(average)));
                    Check(std::abs(int((*lowMaps[m])[i+c])-expected)<=1,"AA color differs from linear-light reduction");
                }
            }
        }
    }
}

void AntialiasingTests(const std::filesystem::path& folder)
{
    rk::RenderSettings settings;settings.width=32;settings.height=24;
    settings.lighting.ambientIntensity=.4f;settings.lighting.directionalIntensity=.35f;
    Check(settings.aaGridSize==1,"Default AA must preserve existing output");
    for(int invalid:{0,3,5,16,-1})
    {
        auto bad=settings;bad.aaGridSize=invalid;bool rejected=false;
        try{bad.Validate();}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"Unsupported AA grid accepted");
    }
    for(bool cutout:{false,true})
    {
        const auto scene=Card(true,cutout);
        const auto frame=rk::FitFrame(*scene.geometry,settings);
        for(int grid:{2,4,8})for(unsigned view=0;view<4;++view)
        {
            auto aa=settings;aa.aaGridSize=grid;
            // Include AO and the invisible ground in the reference comparison.
            aa.ambientOcclusion={true,.7f,.7f,16,true};
            std::atomic<int> rows{0};const auto image=rk::Render(scene,aa,frame,view,{},&rows);
            Check(rows==settings.height && image.width==settings.width && image.height==settings.height,
                  "AA changed output dimensions or row progress");
            auto high=aa;high.aaGridSize=1;high.width*=grid;high.height*=grid;
            CompareReduction(image,rk::Render(scene,high,frame,view),grid);
            Check(Same(image,rk::Render(scene,aa,frame,view)),"AA is not deterministic");
            int partial=0;
            for(std::size_t i=0;i<image.rgba.size();i+=4)
            {
                const auto alpha=image.rgba[i+3];
                if(alpha>0 && alpha<255)++partial;
                Check(alpha==image.albedo[i+3] && alpha==image.directional[i+3],"AA map coverage differs");
                if(!alpha)for(int c=0;c<3;++c)
                    Check(!image.rgba[i+c] && !image.albedo[i+c] && !image.directional[i+c],"Misses are not transparent black");
            }
            Check(partial>10,"AA did not produce fractional edge coverage");
        }
    }
    // Uniform colored edges retain their full straight RGB, never a dark fringe.
    const auto plain=Card(false,false);const auto frame=rk::FitFrame(*plain.geometry,settings);
    auto aa=settings;aa.aaGridSize=4;
    const auto image=rk::Render(plain,aa,frame,0);
    const auto holes=rk::Render(Card(false,true),aa,frame,0);
    int interiorCutouts=0;
    for(std::size_t i=0;i<image.rgba.size();i+=4)
    {
        if(image.rgba[i+3])for(int c=0;c<3;++c)
        {
            const float factors[]{.8f,.05f,.1f};
            Check(std::abs(image.albedo[i+c]-std::lround(255*rk::LinearToSrgb(factors[c])))<=1,
                  "Transparent samples darkened straight edge color");
        }
        if(image.rgba[i+3]==255 && holes.rgba[i+3]>0 && holes.rgba[i+3]<255)++interiorCutouts;
    }
    Check(interiorCutouts>5,"Cutout boundaries inside the mesh were not antialiased");
    auto night=aa;night.lighting.ambientIntensity=night.lighting.directionalIntensity=0;
    const auto dark=rk::Render(plain,night,frame,0);
    Check(dark.albedo==image.albedo && dark.directional==image.directional,"AA baked lighting into reusable maps");
    for(std::size_t i=0;i<dark.rgba.size();i+=4)
        Check(!dark.rgba[i] && !dark.rgba[i+1] && !dark.rgba[i+2] && dark.rgba[i+3]==image.rgba[i+3],
              "Dark surfaces lost coverage alpha");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{rk::Render(plain,aa,frame,0,stop.get_token());}catch(const std::runtime_error&){cancelled=true;}
    Check(cancelled,"AA ignored cancellation");
    const auto output=rk::ExportViews(plain,aa,folder,"aa",true);
    std::ifstream file(output/"sprites.json");const auto metadata=nlohmann::json::parse(file);
    Check(metadata["anti_aliasing"]["grid_size"]==4 && metadata["anti_aliasing"]["samples_per_pixel"]==16 &&
          metadata["alpha"].get<std::string>().find("coverage")!=std::string::npos,"Export lost AA metadata");
    for(unsigned view=0;view<4;++view)
    {
        const auto expected=rk::Render(plain,aa,frame,view);const auto name="aa_view"+std::to_string(view);
        Check(rk::LoadPng(output/(name+".png"))->rgba==expected.rgba &&
              rk::LoadPng(output/(name+"_albedo.png"))->rgba==expected.albedo &&
              rk::LoadPng(output/(name+"_sun.png"))->rgba==expected.directional,"AA PNG export differs from preview");
    }
    std::cout<<"PASS: all AA qualities/views, independent supersized reference, linear filtering, cutout coverage, AO/ground, straight alpha, exports and cancellation\n";
}
