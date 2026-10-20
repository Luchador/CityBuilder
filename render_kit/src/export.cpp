#include "export.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace renderkit
{
namespace
{
    std::string JsonString(const std::string& text)
    {
        std::ostringstream out;out<<'"';
        for(unsigned char c:text)
        {
            if(c=='"' || c=='\\')out<<'\\'<<c;
            else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c)<<std::dec;
            else out<<c;
        }
        out<<'"';return out.str();
    }
    void WriteVector(std::ostream& out,Vec3 value) {out<<'['<<value.x<<", "<<value.y<<", "<<value.z<<']';}
}
std::filesystem::path ExportViews(const Scene& scene,const RenderSettings& settings,
    const std::filesystem::path& parent,const std::string& prefix,bool lightingLayers,
    std::stop_token stop,std::atomic<int>* completedRows)
{
    settings.Validate();
    if(!scene.geometry)throw std::invalid_argument("Import a model first");
    if(prefix.empty() || prefix.size()>80 || prefix.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::invalid_argument("File prefix must use letters, numbers, underscores or hyphens (1..80 characters)");
    if(parent.empty() || !std::filesystem::is_directory(parent))throw std::invalid_argument("Choose an existing export folder");
    const Frame frame=FitFrame(*scene.geometry,settings);
    std::filesystem::path folder;
    for(int i=1;i<=9999;++i)
    {
        std::ostringstream name;name<<prefix<<'_'<<std::setw(3)<<std::setfill('0')<<i;
        auto candidate=parent/NativePath(name.str());
        if(std::filesystem::create_directory(candidate)) {folder=std::move(candidate);break;}
    }
    if(folder.empty())throw std::runtime_error("Cannot find an unused export folder name");
    try
    {
        for(unsigned view=0;view<4;++view)
        {
            if(stop.stop_requested())throw std::runtime_error("Export cancelled");
            const auto image=Render(scene,settings,frame,view,stop,completedRows);
            const auto name=prefix+"_view"+std::to_string(view);
            SavePng(folder/NativePath(name+".png"),image.width,image.height,image.rgba);
            if(lightingLayers)
            {
                SavePng(folder/NativePath(name+"_albedo.png"),image.width,image.height,image.albedo);
                SavePng(folder/NativePath(name+"_sun.png"),image.width,image.height,image.directional);
            }
        }
        if(stop.stop_requested())throw std::runtime_error("Export cancelled");
        std::ofstream metadata(folder/"sprites.json",std::ios::binary);
        if(!metadata)throw std::runtime_error("Cannot write sprite metadata");
        metadata<<std::setprecision(9)<<"{\n  \"version\": 1,\n  \"source\": "<<JsonString(scene.source)
                <<",\n  \"width\": "<<settings.width<<",\n  \"height\": "<<settings.height
                <<",\n  \"pixels_per_model_unit\": "<<frame.pixelsPerUnit
                <<",\n  \"projection\": \"orthographic\",\n  \"pitch_degrees\": 33,\n"
                <<"  \"alpha\": \"straight binary: 255=accepted hit, 0=miss\",\n  \"frame_center\": ";
        WriteVector(metadata,frame.center);metadata<<",\n  \"ground_pivot\": ";WriteVector(metadata,frame.pivot);
        metadata<<",\n  \"light_direction_toward_source\": ";WriteVector(metadata,DirectionalLightDirection);
        metadata<<",\n  \"ambient_color\": ";WriteVector(metadata,settings.lighting.ambientColor);
        metadata<<",\n  \"directional_color\": ";WriteVector(metadata,settings.lighting.directionalColor);
        metadata<<",\n  \"ambient_intensity\": "<<settings.lighting.ambientIntensity
                <<",\n  \"directional_intensity\": "<<settings.lighting.directionalIntensity
                <<",\n  \"lighting_divisor\": "<<LightingDivisor
                <<",\n  \"shadows\": "<<(settings.lighting.shadows?"true":"false")
                <<",\n  \"lighting_layers\": "<<(lightingLayers?"true":"false")
                <<",\n  \"ambient_occlusion\": {\"enabled\": "<<(settings.ambientOcclusion.enabled?"true":"false")
                <<", \"intensity\": "<<settings.ambientOcclusion.intensity
                <<", \"radius_model_units\": "<<settings.ambientOcclusion.radius
                <<", \"samples\": "<<settings.ambientOcclusion.samples
                <<", \"bake_target\": \"linear albedo before sRGB encoding\"}"
                <<",\n  \"lighting_formula\": \"displayRGB = albedo_sRGB * (ambient_color * ambient_intensity + directional_color * directional_intensity * sun_mask_linear) / lighting_divisor\",\n"
                <<"  \"views\": [\n";
        for(unsigned view=0;view<4;++view)
        {
            const auto name=prefix+"_view"+std::to_string(view);
            const auto basis=ViewBasis(view);
            metadata<<"    {\"quarter_turns\": "<<view<<", \"yaw_degrees\": "<<45+90*view
                    <<", \"file\": "<<JsonString(name+".png")<<", \"pivot_pixels\": ["
                    <<frame.pivotPixels[view].x<<", "<<frame.pivotPixels[view].y<<"], \"right\": ";
            WriteVector(metadata,basis.right);metadata<<", \"up\": ";WriteVector(metadata,basis.up);
            metadata<<", \"toward_camera\": ";WriteVector(metadata,basis.towardCamera);
            if(lightingLayers)metadata<<", \"albedo\": "<<JsonString(name+"_albedo.png")<<", \"sun_mask\": "<<JsonString(name+"_sun.png");
            metadata<<'}'<<(view==3?"\n":",\n");
        }
        metadata<<"  ]\n}\n";metadata.close();
        if(!metadata)throw std::runtime_error("Failed to finish sprite metadata");
        return folder;
    }
    catch(...)
    {
        std::error_code ignored;
        std::filesystem::remove_all(folder,ignored); // Only the folder this call created.
        throw;
    }
}
}
