#include "scene_file.hpp"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace renderkit
{
namespace
{
    using Json=nlohmann::json;
    constexpr std::size_t MaxSceneBytes=16*1024*1024;
    [[noreturn]] void Invalid(const std::string& message){throw std::runtime_error("Invalid Render Kit scene: "+message);}
    std::string String(const Json& value)
    {
        const auto result=value.get<std::string>();
        if(result.size()>32768 || result.find('\0')!=std::string::npos)Invalid("invalid text or path");
        return result;
    }
    int Integer(const Json& value,int minimum,int maximum)
    {
        if(!value.is_number_integer())Invalid("expected a whole number");
        const double number=value.get<double>();
        if(number<minimum || number>maximum)Invalid("integer is outside its supported range");
        return static_cast<int>(number);
    }
    float Number(const Json& value,float minimum=-std::numeric_limits<float>::max(),float maximum=std::numeric_limits<float>::max())
    {
        if(!value.is_number())Invalid("expected a number");
        const double number=value.get<double>();
        if(!std::isfinite(number) || number<minimum || number>maximum)Invalid("number is outside its supported range");
        return static_cast<float>(number);
    }
    void Array(const Json& value,std::size_t count)
    {if(!value.is_array() || value.size()!=count)Invalid("incorrect vector length");}
    Vec2 ReadVec2(const Json& value)
    {Array(value,2);return {Number(value[0]),Number(value[1])};}
    Vec3 ReadColor(const Json& value)
    {Array(value,3);return {Number(value[0],0,1),Number(value[1],0,1),Number(value[2],0,1)};}
    Vec4 ReadFactor(const Json& value)
    {Array(value,4);return {Number(value[0],0,1),Number(value[1],0,1),Number(value[2],0,1),Number(value[3],0,1)};}
    Json Vector(Vec2 v){return Json::array({v.x,v.y});}
    Json Vector(Vec3 v){return Json::array({v.x,v.y,v.z});}
    Json Vector(Vec4 v){return Json::array({v.r,v.g,v.b,v.a});}
    std::filesystem::path Absolute(const std::filesystem::path& path)
    {return std::filesystem::absolute(path).lexically_normal();}
    std::string Reference(const std::filesystem::path& asset,const std::filesystem::path& folder)
    {
        if(asset.empty())Invalid("an asset or output path is empty");
        const auto absolute=Absolute(asset);
        const auto relative=absolute.lexically_relative(folder);
        const auto text=(relative.empty()?absolute:relative).generic_u8string();
        return {text.begin(),text.end()};
    }
    std::filesystem::path Resolve(const Json& value,const std::filesystem::path& folder)
    {
        auto text=String(value);
        if(text.empty())Invalid("an asset or output path is empty");
        // Portable separators also accept Windows-authored .rks files on other OSes.
        std::replace(text.begin(),text.end(),'\\','/');
        return Absolute(folder/NativePath(text));
    }
    void CheckPrefix(const std::string& prefix)
    {
        if(prefix.empty() || prefix.size()>80 || prefix.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
            Invalid("output prefix must use letters, numbers, underscores or hyphens (1..80 characters)");
    }
    Json TextureReference(const std::shared_ptr<const Image>& image,const std::filesystem::path& folder)
    {
        if(!image)return nullptr; // A cleared texture must stay cleared on reload.
        if(image->sourcePath.empty())return Json{{"kind","model"}};
        return Json{{"kind","file"},{"path",Reference(image->sourcePath,folder)}};
    }
    Json WriteMaterial(const Material& m,const std::filesystem::path& folder)
    {
        return {
            {"name",m.name},{"base_color",TextureReference(m.baseColor,folder)},{"opacity",TextureReference(m.opacity,folder)},
            {"base_factor",Vector(m.baseFactor)},{"alpha_mode",static_cast<int>(m.alphaMode)},
            {"opacity_channel",static_cast<int>(m.opacityChannel)},{"cutoff",m.cutoff},{"double_sided",m.doubleSided},
            {"texcoord",m.texcoord},{"uv_offset",Vector(m.uvOffset)},{"uv_scale",Vector(m.uvScale)},{"uv_rotation",m.uvRotation},
            {"wrap_u",static_cast<int>(m.wrapU)},{"wrap_v",static_cast<int>(m.wrapV)},
            {"use_base_color_alpha",m.useBaseColorAlpha},{"invert_opacity",m.invertOpacity},{"opacity_strength",m.opacityStrength},
            {"separate_opacity_uv",m.separateOpacityUv},{"opacity_wrap_u",static_cast<int>(m.opacityWrapU)},
            {"opacity_wrap_v",static_cast<int>(m.opacityWrapV)}};
    }
    void ReadMaterialSettings(const Json& j,Material& m)
    {
        m.name=String(j.at("name"));m.baseFactor=ReadFactor(j.at("base_factor"));
        m.alphaMode=static_cast<AlphaMode>(Integer(j.at("alpha_mode"),0,1));
        m.opacityChannel=static_cast<OpacityChannel>(Integer(j.at("opacity_channel"),0,2));
        m.cutoff=Number(j.at("cutoff"),0,1);m.doubleSided=j.at("double_sided").get<bool>();
        m.texcoord=Integer(j.at("texcoord"),0,1);m.uvOffset=ReadVec2(j.at("uv_offset"));
        m.uvScale=ReadVec2(j.at("uv_scale"));m.uvRotation=Number(j.at("uv_rotation"));
        m.wrapU=static_cast<Wrap>(Integer(j.at("wrap_u"),0,2));m.wrapV=static_cast<Wrap>(Integer(j.at("wrap_v"),0,2));
        m.useBaseColorAlpha=j.at("use_base_color_alpha").get<bool>();m.invertOpacity=j.at("invert_opacity").get<bool>();
        m.opacityStrength=Number(j.at("opacity_strength"),0,1);m.separateOpacityUv=j.at("separate_opacity_uv").get<bool>();
        m.opacityWrapU=static_cast<Wrap>(Integer(j.at("opacity_wrap_u"),0,2));
        m.opacityWrapV=static_cast<Wrap>(Integer(j.at("opacity_wrap_v"),0,2));
    }
    void ReadOptions(const Json& root,SceneDocument& document,const std::filesystem::path& folder)
    {
        const auto& render=root.at("render");auto& settings=document.settings;
        settings.width=Integer(render.at("width"),16,4096);settings.height=Integer(render.at("height"),16,4096);
        settings.padding=Number(render.at("padding"),0,.45f);settings.autoFit=render.at("auto_fit").get<bool>();
        settings.pixelsPerUnit=Number(render.at("pixels_per_unit"));
        const auto& light=root.at("lighting");auto& lighting=settings.lighting;
        lighting.ambientColor=ReadColor(light.at("ambient_color"));lighting.directionalColor=ReadColor(light.at("directional_color"));
        lighting.ambientIntensity=Number(light.at("ambient_intensity"));lighting.directionalIntensity=Number(light.at("directional_intensity"));
        lighting.shadows=light.at("shadows").get<bool>();
        // Optional in version 1: scenes saved before AO was added keep it disabled.
        settings.ambientOcclusion=AmbientOcclusion{};
        if(root.contains("ambient_occlusion"))
        {
            const auto& encoded=root.at("ambient_occlusion");auto& ao=settings.ambientOcclusion;
            ao.enabled=encoded.at("enabled").get<bool>();ao.intensity=Number(encoded.at("intensity"));
            ao.radius=Number(encoded.at("radius"));ao.samples=Integer(encoded.at("samples"),16,256);
        }
        settings.Validate();
        const auto& output=root.at("output");document.outputDirectory=Resolve(output.at("directory"),folder);
        document.outputPrefix=String(output.at("prefix"));CheckPrefix(document.outputPrefix);
        document.exportLayers=output.at("lighting_layers").get<bool>();
        const auto& preview=root.at("preview");document.view=Integer(preview.at("view"),0,3);
        document.previewChannel=Integer(preview.at("channel"),0,3);
        document.selectedMaterial=Integer(preview.at("material"),0,100000);
    }
    void CheckStop(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Scene load cancelled");}
    void WriteFile(const std::filesystem::path& path,const std::string& contents)
    {
        if(!std::filesystem::is_directory(path.parent_path()))throw std::runtime_error("Scene folder does not exist: "+Utf8Path(path.parent_path()));
        // Reserve a private sibling directory so concurrent saves never share a temporary file.
        std::filesystem::path temporary;
        for(int i=0;i<100;++i)
        {
            const auto candidate=path.parent_path()/NativePath(".render-kit-save-"+std::to_string(SDL_GetTicksNS())+"-"+std::to_string(i));
            if(std::filesystem::create_directory(candidate)){temporary=candidate;break;}
        }
        if(temporary.empty())throw std::runtime_error("Cannot create temporary scene file");
        const auto filePath=temporary/"scene.rks";
        try
        {
            std::ofstream file(filePath,std::ios::binary|std::ios::trunc);
            if(!file)throw std::runtime_error("Cannot write temporary scene file");
            file.write(contents.data(),static_cast<std::streamsize>(contents.size()));file.close();
            if(!file)throw std::runtime_error("Failed to finish writing scene file");
            if(!SDL_RenamePath(Utf8Path(filePath).c_str(),Utf8Path(path).c_str()))
                throw std::runtime_error(std::string("Cannot replace scene file: ")+SDL_GetError());
        }
        catch(...){std::error_code ignored;std::filesystem::remove_all(temporary,ignored);throw;}
        std::error_code ignored;std::filesystem::remove(temporary,ignored);
    }
}

bool IsSceneFile(const std::filesystem::path& path)
{
    auto extension=Utf8Path(path.extension());
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return extension==".rks";
}

void SaveSceneFile(const std::filesystem::path& path,const SceneDocument& document)
{
    try
    {
        if(!IsSceneFile(path))throw std::runtime_error("Render Kit scene files must use the .rks extension");
        if(!document.scene.geometry || document.scene.source.empty() || document.scene.materials.empty())
            throw std::runtime_error("Import a model before saving a scene");
        const auto destination=Absolute(path),folder=destination.parent_path();
        const auto& settings=document.settings;const auto& light=settings.lighting;
        const auto& ao=settings.ambientOcclusion;
        Json root={
            {"format","CitiesUnlimited.RenderKit"},{"version",1},{"model",Reference(NativePath(document.scene.source),folder)},
            {"render",{{"width",settings.width},{"height",settings.height},{"padding",settings.padding},{"auto_fit",settings.autoFit},{"pixels_per_unit",settings.pixelsPerUnit}}},
            {"lighting",{{"ambient_color",Vector(light.ambientColor)},{"directional_color",Vector(light.directionalColor)},
                         {"ambient_intensity",light.ambientIntensity},{"directional_intensity",light.directionalIntensity},{"shadows",light.shadows}}},
            {"ambient_occlusion",{{"enabled",ao.enabled},{"intensity",ao.intensity},{"radius",ao.radius},{"samples",ao.samples}}},
            {"output",{{"directory",Reference(document.outputDirectory,folder)},{"prefix",document.outputPrefix},{"lighting_layers",document.exportLayers}}},
            {"preview",{{"view",document.view},{"channel",document.previewChannel},{"material",document.selectedMaterial}}},
            {"materials",Json::array()}};
        for(const auto& material:document.scene.materials)
        {
            auto encoded=WriteMaterial(material,folder);Material checked;
            ReadMaterialSettings(encoded,checked); // Reject NaNs/invalid enums before touching an existing save.
            root["materials"].push_back(std::move(encoded));
        }
        SceneDocument checked;ReadOptions(root,checked,folder);
        if(static_cast<std::size_t>(checked.selectedMaterial)>=document.scene.materials.size())Invalid("selected material is out of range");
        const auto contents=root.dump(2)+'\n';
        if(contents.size()>MaxSceneBytes)throw std::runtime_error("Scene settings exceed 16 MiB");
        WriteFile(destination,contents);
    }
    catch(const Json::exception& e){Invalid(e.what());}
}

SceneDocument LoadSceneFile(const std::filesystem::path& path,std::stop_token stop)
{
    try
    {
        CheckStop(stop);
        const auto absolute=Absolute(path),folder=absolute.parent_path();
        std::ifstream file(absolute,std::ios::binary|std::ios::ate);
        if(!file)throw std::runtime_error("Cannot open scene: "+Utf8Path(absolute));
        const auto size=file.tellg();
        if(size<=0 || size>static_cast<std::streamoff>(MaxSceneBytes))Invalid("file is empty or exceeds 16 MiB");
        std::string contents(static_cast<std::size_t>(size),'\0');file.seekg(0);
        if(!file.read(contents.data(),static_cast<std::streamsize>(contents.size())))throw std::runtime_error("Cannot read scene file");
        const auto root=Json::parse(contents,[](int depth,Json::parse_event_t,Json&){if(depth>64)Invalid("JSON nesting is too deep");return true;});
        if(String(root.at("format"))!="CitiesUnlimited.RenderKit")Invalid("unrecognized file format");
        if(Integer(root.at("version"),0,100000)!=1)Invalid("unsupported scene version; use a compatible Render Kit");
        SceneDocument document;ReadOptions(root,document,folder);
        const auto& saved=root.at("materials");
        if(!saved.is_array() || saved.empty() || saved.size()>100000)Invalid("invalid material list");
        std::vector<Material> settings;settings.reserve(saved.size());
        for(const auto& j:saved){Material m;ReadMaterialSettings(j,m);settings.push_back(std::move(m));}
        if(static_cast<std::size_t>(document.selectedMaterial)>=settings.size())Invalid("selected material is out of range");
        const auto model=Resolve(root.at("model"),folder);
        if(!std::filesystem::is_regular_file(model))throw std::runtime_error("Scene model is missing: "+Utf8Path(model));
        CheckStop(stop);document.scene=LoadModel(model);CheckStop(stop);
        if(document.scene.materials.size()!=saved.size())Invalid("the model's material list has changed; reimport the model to create a new scene");
        std::unordered_map<std::string,std::shared_ptr<const Image>> images;
        const auto texture=[&](const Json& ref,const std::shared_ptr<const Image>& imported)->std::shared_ptr<const Image>
        {
            if(ref.is_null())return {};
            const auto kind=String(ref.at("kind"));
            if(kind=="model")
            {
                if(!imported)throw std::runtime_error("A referenced model texture is missing or could not be decoded; check the model's import notes");
                return imported;
            }
            if(kind!="file")Invalid("unrecognized texture reference");
            const auto imagePath=Resolve(ref.at("path"),folder);const auto key=Utf8Path(imagePath);
            if(auto it=images.find(key);it!=images.end())return it->second;
            CheckStop(stop);
            try{auto image=LoadPng(imagePath);images.emplace(key,image);return image;}
            catch(const std::exception& e){throw std::runtime_error("Scene texture could not be loaded: "+key+". "+e.what());}
        };
        for(std::size_t i=0;i<settings.size();++i)
        {
            auto& material=settings[i];const auto& imported=document.scene.materials[i];
            if(material.name!=imported.name)Invalid("material "+std::to_string(i)+" changed name/order in the model; reimport to create a new scene");
            try
            {
                material.baseColor=texture(saved[i].at("base_color"),imported.baseColor);
                material.opacity=texture(saved[i].at("opacity"),imported.opacity);
            }
            catch(const std::exception& e){throw std::runtime_error("Material '"+material.name+"': "+e.what());}
        }
        document.scene.materials=std::move(settings);
        if(!std::filesystem::is_directory(document.outputDirectory))
            document.scene.warnings.push_back("Saved export folder does not exist: "+Utf8Path(document.outputDirectory)+". Choose a folder before exporting.");
        CheckStop(stop);return document;
    }
    catch(const Json::exception& e){Invalid(e.what());}
}
}
