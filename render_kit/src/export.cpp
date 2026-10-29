#include "export.hpp"
#include "lighting_dds.hpp"
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
    const auto& gi=settings.globalIllumination;
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
                if(gi.enabled)
                {
                    SaveLightingDds(folder/NativePath(name+"_direct.dds"),image.width,image.height,image.directLight,image.rgba);
                    SaveLightingDds(folder/NativePath(name+"_indirect.dds"),image.width,image.height,image.indirectLight,image.rgba);
                    SaveLightingDds(folder/NativePath(name+"_sky.dds"),image.width,image.height,image.skyLight,image.rgba);
                }
            }
        }
        if(stop.stop_requested())throw std::runtime_error("Export cancelled");
        std::ofstream metadata(folder/"sprites.json",std::ios::binary);
        if(!metadata)throw std::runtime_error("Cannot write sprite metadata");
        metadata<<std::setprecision(9)<<"{\n  \"version\": "<<(gi.enabled?2:1)<<",\n  \"source\": "<<JsonString(scene.source)
                <<",\n  \"width\": "<<settings.width<<",\n  \"height\": "<<settings.height
                <<",\n  \"pixels_per_model_unit\": "<<frame.pixelsPerUnit
                <<",\n  \"projection\": \"orthographic\",\n  \"pitch_degrees\": 33,\n"
                <<"  \"alpha\": "<<JsonString(settings.aaGridSize==1?"PNG straight binary: 255=accepted hit, 0=miss":
                    "PNG straight coverage: 0=miss, 255=fully covered, intermediate=partial coverage")
                <<",\n  \"anti_aliasing\": {\"method\": \"supersampling\", \"grid_size\": "<<settings.aaGridSize
                <<", \"samples_per_pixel\": "<<settings.aaGridSize*settings.aaGridSize
                <<", \"resolve\": \"coverage-weighted linear-light RGB; linear sun mask; straight alpha\"}"
                <<",\n  \"frame_center\": ";
        WriteVector(metadata,frame.center);metadata<<",\n  \"ground_pivot\": ";WriteVector(metadata,frame.pivot);
        metadata<<",\n  \"light_direction_toward_source\": ";WriteVector(metadata,gi.enabled?Normalize(DirectionalLightDirection):DirectionalLightDirection);
        metadata<<",\n  \"ambient_color\": ";WriteVector(metadata,settings.lighting.ambientColor);
        metadata<<",\n  \"directional_color\": ";WriteVector(metadata,settings.lighting.directionalColor);
        metadata<<",\n  \"ambient_intensity\": "<<settings.lighting.ambientIntensity
                <<",\n  \"directional_intensity\": "<<settings.lighting.directionalIntensity
                <<",\n  \"lighting_divisor\": "<<(gi.enabled?1.0f:LightingDivisor)
                <<",\n  \"shadows\": "<<(settings.lighting.shadows?"true":"false")
                <<",\n  \"lighting_layers\": "<<(lightingLayers?"true":"false")
                <<",\n  \"ambient_occlusion\": {\"enabled\": "<<(settings.ambientOcclusion.enabled?"true":"false")
                <<", \"applied\": "<<(!gi.enabled && settings.ambientOcclusion.enabled?"true":"false")
                <<", \"intensity\": "<<settings.ambientOcclusion.intensity
                <<", \"radius_model_units\": "<<settings.ambientOcclusion.radius
                <<", \"samples\": "<<settings.ambientOcclusion.samples
                <<", \"use_ground_plane\": "<<(settings.ambientOcclusion.useGroundPlane?"true":"false")
                <<", \"bake_target\": \"linear albedo before sRGB encoding\"}"
                <<",\n  \"global_illumination\": {\"enabled\": "<<(gi.enabled?"true":"false")
                <<", \"samples_per_aa_sample\": "<<gi.samples<<", \"bounces\": "<<gi.bounces
                <<", \"strength\": "<<gi.strength<<", \"use_ground_plane\": "<<(gi.useGroundPlane?"true":"false")
                <<", \"ground_color_linear\": ";WriteVector(metadata,gi.groundColor);
        metadata<<", \"sky\": \"uniform white upper hemisphere; black lower hemisphere\","
                <<" \"layer_encoding\": \"DX10 DDS RGBA16_FLOAT; linear HDR premultiplied RGB; shared coverage alpha; no mipmaps\","
                <<" \"sun_moon_shared_direction\": true, \"contains_material_color\": true}"
                <<",\n  \"lighting_model\": "<<JsonString(gi.enabled?"diffuse_gi_linear":"legacy_display_space")
                <<",\n  \"lighting_formula\": "<<JsonString(gi.enabled?
                    "linearPremultipliedRGB = (direct + indirect * gi_strength) * srgbToLinear(directional_color) * directional_intensity + sky * srgbToLinear(ambient_color) * ambient_intensity":
                    "displayRGB = albedo_sRGB * (ambient_color * ambient_intensity + directional_color * directional_intensity * sun_mask_linear) / lighting_divisor")
                <<",\n  \"lighting_formula_note\": "<<JsonString(gi.enabled?
                    "DDS maps already include material color. Do not multiply albedo or AO again. Use premultiplied alpha blending (ONE, ONE_MINUS_SRC_ALPHA) in a linear framebuffer; display-encode afterward. Albedo and sun PNGs alone omit GI. Camera views keep light fixed in world space.":
                    "Applied per sample. With anti-aliasing, independently resolved albedo and sun maps approximate the resolved lit sprite at mixed-color or mixed-lighting pixels.")
                <<",\n  \"views\": [\n";
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
            if(lightingLayers && gi.enabled)metadata<<", \"direct_light\": "<<JsonString(name+"_direct.dds")
                <<", \"indirect_light\": "<<JsonString(name+"_indirect.dds")<<", \"sky_light\": "<<JsonString(name+"_sky.dds");
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
