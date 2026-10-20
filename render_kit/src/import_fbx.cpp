#include "scene.hpp"
#include <ufbx.h>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace renderkit
{
namespace
{
    std::string Text(ufbx_string value) { return {value.data,value.length}; }
    Vec3 Vector(ufbx_vec3 v) { return {static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)}; }
    float Scalar(ufbx_real value)
    {
        if(!std::isfinite(value))throw std::runtime_error("FBX contains a nonfinite material value");
        return static_cast<float>(std::clamp(value,0.0,1.0));
    }
    float Average(const ufbx_material_map& map)
    {
        return map.value_components>=3 ? Scalar((map.value_vec3.x+map.value_vec3.y+map.value_vec3.z)/3) : Scalar(map.value_real);
    }
    const ufbx_texture* Texture(const ufbx_material_map& map)
    { return map.texture_enabled ? map.texture : nullptr; }
    Wrap TextureWrap(ufbx_wrap_mode mode) { return mode==UFBX_WRAP_CLAMP ? Wrap::Clamp : Wrap::Repeat; }
    bool HasAlpha(const Image& image)
    {
        for(std::size_t i=3;i<image.rgba.size();i+=4)if(image.rgba[i]!=255)return true;
        return false;
    }
    std::filesystem::path TexturePath(ufbx_string text)
    {
        auto name=Text(text);std::replace(name.begin(),name.end(),'\\','/');
        return NativePath(name);
    }
    struct MaterialTextures { const ufbx_texture* base=nullptr;const ufbx_texture* opacity=nullptr; };
    struct UvSource { const ufbx_vertex_vec2* values=nullptr;const ufbx_texture* texture=nullptr; };
    UvSource FindUv(const ufbx_mesh& mesh,const ufbx_texture* texture)
    {
        const auto* values=&mesh.vertex_uv;
        if(texture && texture->uv_set.length)
        {
            values=nullptr;
            for(const auto& set:mesh.uv_sets)
                if(Text(set.name)==Text(texture->uv_set)){values=&set.vertex_uv;break;}
            if(!values)throw std::runtime_error("FBX texture refers to a missing UV set: "+Text(texture->uv_set));
        }
        return {values,texture};
    }
    Vec2 ReadUv(UvSource source,std::uint32_t index)
    {
        if(!source.values || !source.values->exists)return {};
        auto value=ufbx_get_vertex_vec2(source.values,index);
        ufbx_vec3 uv{value.x,value.y,0};
        if(source.texture)uv=ufbx_transform_position(&source.texture->uv_to_texture,uv);
        // FBX UVs start at the bottom; decoded PNG rows and glTF start at the top.
        return {static_cast<float>(uv.x),static_cast<float>(1-uv.y)};
    }
}

Scene LoadFbx(const std::filesystem::path& path)
{
    // Read with filesystem::path for Unicode paths on Windows as well as Linux.
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open "+Utf8Path(path));
    const auto size=file.tellg();
    if(size<=0 || size>512ll*1024*1024)throw std::runtime_error("FBX is empty or exceeds 512 MiB");
    std::vector<char> bytes(static_cast<std::size_t>(size));file.seekg(0);
    if(!file.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read FBX");
    const auto filename=Utf8Path(std::filesystem::absolute(path));
    ufbx_load_opts options{};
    options.filename={filename.c_str(),filename.size()};
    options.file_format=UFBX_FILE_FORMAT_FBX;
    options.target_axes=ufbx_axes_right_handed_y_up;
    options.target_unit_meters=1;
    options.space_conversion=UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    options.generate_missing_normals=true;
    options.use_blender_pbr_material=true;
    options.node_depth_limit=256;
    options.index_error_handling=UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    ufbx_error error{};
    std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> input(
        ufbx_load_memory(bytes.data(),bytes.size(),&options,&error),ufbx_free_scene);
    if(!input)
    {
        char message[1024]{};ufbx_format_error(message,sizeof(message),&error);
        throw std::runtime_error(std::string("Import FBX: ")+message);
    }
    Scene scene;scene.source=Utf8Path(std::filesystem::absolute(path).lexically_normal());
    if(input->anim_stacks.count)scene.warnings.push_back("FBX animations are not played; importing the default static pose. Bake wind/growth before export.");
    for(const auto& warning:input->metadata.warnings)scene.warnings.push_back("FBX: "+Text(warning.description));
    scene.warnings.push_back("FBX axes converted to right-handed Y-up; file units converted to meters. One meter is one Render Kit model unit.");
    std::unordered_set<std::string> notes;
    const auto note=[&](const std::string& message){if(notes.insert(message).second)scene.warnings.push_back(message);};
    std::unordered_map<const ufbx_texture*,std::shared_ptr<const Image>> images;
    const auto loadImage=[&](const ufbx_texture* texture)->std::shared_ptr<const Image>
    {
        if(!texture)return {};
        if(auto found=images.find(texture);found!=images.end())return found->second;
        std::shared_ptr<const Image> result;
        try
        {
            if(texture->type!=UFBX_TEXTURE_FILE)
                throw std::runtime_error("Layered/procedural textures must be baked to PNG");
            const auto content=texture->content.size ? texture->content : texture->video ? texture->video->content : ufbx_blob{};
            if(content.size)
                result=DecodePng({static_cast<const std::uint8_t*>(content.data),content.size},Text(texture->name)+" (embedded)");
            else
            {
                std::vector<std::filesystem::path> candidates;
                // Prefer portable paths beside the model over stale absolute authoring paths.
                for(auto name:{texture->relative_filename,texture->filename,texture->absolute_filename})
                {
                    if(!name.length)continue;
                    const auto imagePath=TexturePath(name);
                    if(!imagePath.is_absolute())candidates.push_back(path.parent_path()/imagePath);
                    candidates.push_back(path.parent_path()/imagePath.filename());
                }
                for(auto name:{texture->filename,texture->absolute_filename})
                    if(name.length && TexturePath(name).is_absolute())candidates.push_back(TexturePath(name));
                for(const auto& candidate:candidates)
                {
                    std::error_code ec;
                    if(std::filesystem::is_regular_file(candidate,ec)){result=LoadPng(candidate);break;}
                }
                if(!result)throw std::runtime_error("PNG not found: "+Text(texture->relative_filename.length?texture->relative_filename:texture->filename));
            }
        }
        catch(const std::exception& e){note(Text(texture->name)+": "+e.what()+". Assign a PNG in Materials.");}
        images.emplace(texture,result);return result;
    };
    std::vector<MaterialTextures> textures;
    for(const auto* source:input->materials)
    {
        Material material;MaterialTextures maps;
        material.name=source->name.length?Text(source->name):"Material "+std::to_string(scene.materials.size()+1);
        const auto& color=source->pbr.base_color.has_value || Texture(source->pbr.base_color) ? source->pbr.base_color : source->fbx.diffuse_color;
        const auto& factor=source->pbr.base_factor.has_value ? source->pbr.base_factor : source->fbx.diffuse_factor;
        if(color.has_value)material.baseFactor={Scalar(color.value_vec3.x),Scalar(color.value_vec3.y),Scalar(color.value_vec3.z),1};
        const float strength=factor.has_value?Scalar(factor.value_real):1;
        material.baseFactor.r*=strength;material.baseFactor.g*=strength;material.baseFactor.b*=strength;
        maps.base=Texture(color);material.baseColor=loadImage(maps.base);
        if(maps.base){material.wrapU=TextureWrap(maps.base->wrap_u);material.wrapV=TextureWrap(maps.base->wrap_v);}
        const bool baseHasAlpha=material.baseColor && HasAlpha(*material.baseColor);
        const auto& opacity=source->pbr.opacity;
        const auto& transparency=source->fbx.transparency_color;
        const auto& transparencyFactor=source->fbx.transparency_factor;
        if(opacity.has_value || Texture(opacity))
        {
            material.baseFactor.a=opacity.has_value?Average(opacity):1;
            maps.opacity=Texture(opacity);
        }
        else if(Texture(transparency) || Texture(transparencyFactor))
        {
            maps.opacity=Texture(transparency)?Texture(transparency):Texture(transparencyFactor);
            // A shared diffuse/transparent RGBA image conventionally uses its alpha.
            // Let the base image supply it once, avoiding alpha * alpha.
            const auto mask=loadImage(maps.opacity);
            if(baseHasAlpha && mask && (mask==material.baseColor || mask->name==material.baseColor->name))maps.opacity=nullptr;
            else
            {
                material.invertOpacity=true;
                material.opacityStrength=Texture(transparency) ? (transparencyFactor.has_value?Scalar(transparencyFactor.value_real):1)
                    : (transparency.has_value?Average(transparency):1);
                note(material.name+": legacy FBX transparency map uses white = transparent. Check Invert opacity and Channel if your exporter writes an opacity mask instead.");
            }
        }
        else if(!baseHasAlpha)
        {
            // A base PNG alpha takes priority over ambiguous legacy transparency constants.
            const float transparencyAmount=(transparencyFactor.has_value?Scalar(transparencyFactor.value_real):1)*
                (transparency.has_value?Average(transparency):(transparencyFactor.has_value?1:0));
            material.baseFactor.a=1-transparencyAmount;
        }
        material.opacity=loadImage(maps.opacity);
        if(material.opacity)
        {
            material.useBaseColorAlpha=false; // The explicit mask is authoritative, not a second copy of PNG alpha.
            material.separateOpacityUv=true;
            material.opacityWrapU=TextureWrap(maps.opacity->wrap_u);material.opacityWrapV=TextureWrap(maps.opacity->wrap_v);
            if(baseHasAlpha && (material.opacity==material.baseColor || material.opacity->name==material.baseColor->name))
                material.opacityChannel=OpacityChannel::Alpha;
        }
        const bool cutout=baseHasAlpha || maps.opacity || material.baseFactor.a<1;
        material.alphaMode=cutout?AlphaMode::Cutout:AlphaMode::Opaque;
        // FBX has no universal foliage culling flag. Thin cut-out cards are two-sided by default.
        material.doubleSided=source->features.double_sided.is_explicit?source->features.double_sided.enabled:cutout;
        if(cutout)note(material.name+": imported as alpha cut-out with cutoff 0.5; review foliage settings in Materials.");
        textures.push_back(maps);scene.materials.push_back(std::move(material));
    }
    const auto defaultMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.emplace_back();textures.emplace_back();
    auto geometry=std::make_shared<Geometry>();
    for(const auto* node:input->nodes)
    {
        if(!node->mesh)continue;
        bool visible=true;
        for(const auto* parent=node;parent;parent=parent->parent)if(!parent->visible){visible=false;break;}
        if(!visible)continue;
        const auto& mesh=*node->mesh;
        if(mesh.skin_deformers.count || mesh.blend_deformers.count || mesh.cache_deformers.count)
            throw std::runtime_error("Bake FBX skins, morph targets and vertex caches into a static mesh before importing: "+Text(node->name));
        const auto& matrix=node->geometry_to_world;
        const auto determinant=ufbx_matrix_determinant(&matrix);
        if(!std::isfinite(determinant) || std::abs(determinant)<1e-20)throw std::runtime_error("FBX mesh has a singular transform");
        const auto normalMatrix=ufbx_matrix_for_normals(&matrix);
        if(mesh.num_triangles>5'000'000 || mesh.max_face_triangles>5'000'000)throw std::runtime_error("FBX mesh exceeds five million triangles");
        // Resolve named UV sets once per material instance, not for every polygon.
        std::vector<std::array<UvSource,2>> uvMappings;
        for(const auto* material:node->materials)
        {
            const auto& maps=textures[material?material->typed_id:defaultMaterial];
            uvMappings.push_back({FindUv(mesh,maps.base),FindUv(mesh,maps.opacity)});
        }
        const auto defaultUv=FindUv(mesh,nullptr);
        std::vector<std::uint32_t> indices(static_cast<std::size_t>(mesh.max_face_triangles)*3);
        for(std::size_t faceIndex=0;faceIndex<mesh.faces.count;++faceIndex)
        {
            const auto face=mesh.faces[faceIndex];
            if(face.num_indices<3){note("FBX line/point geometry was skipped.");continue;}
            const auto slot=mesh.face_material.count?mesh.face_material[faceIndex]:UFBX_NO_INDEX;
            const auto* source=slot<node->materials.count?node->materials[slot]:nullptr;
            const auto material=source?source->typed_id:defaultMaterial;
            const auto baseUv=slot<uvMappings.size()?uvMappings[slot][0]:defaultUv;
            const auto opacityUv=slot<uvMappings.size()?uvMappings[slot][1]:defaultUv;
            const auto count=ufbx_triangulate_face(indices.data(),indices.size(),&mesh,face);
            for(std::uint32_t triangleIndex=0;triangleIndex<count;++triangleIndex)
            {
                Triangle triangle;triangle.material=material;
                for(unsigned corner=0;corner<3;++corner)
                {
                    const auto index=indices[triangleIndex*3+corner];auto& vertex=triangle.vertices[corner];
                    vertex.position=Vector(ufbx_transform_position(&matrix,ufbx_get_vertex_vec3(&mesh.vertex_position,index)));
                    const auto normal=Vector(ufbx_transform_direction(&normalMatrix,ufbx_get_vertex_vec3(&mesh.vertex_normal,index)));
                    if(!Finite(normal))throw std::runtime_error("FBX contains a nonfinite normal");
                    vertex.normal=Normalize(normal);vertex.uv={ReadUv(baseUv,index),ReadUv(opacityUv,index)};
                    if(mesh.vertex_color.exists)
                    {
                        const auto c=ufbx_get_vertex_vec4(&mesh.vertex_color,index);
                        vertex.color={static_cast<float>(c.x),static_cast<float>(c.y),static_cast<float>(c.z),static_cast<float>(c.w)};
                    }
                }
                if(determinant<0)std::swap(triangle.vertices[1],triangle.vertices[2]);
                const auto faceNormal=Cross(triangle.vertices[1].position-triangle.vertices[0].position,triangle.vertices[2].position-triangle.vertices[0].position);
                if(Length(faceNormal)<1e-20f)continue;
                if(geometry->triangles.size()>=5'000'000)throw std::runtime_error("FBX exceeds five million triangles including instances");
                geometry->triangles.push_back(std::move(triangle));
            }
        }
    }
    geometry->BuildAcceleration();scene.geometry=std::move(geometry);
    if(std::none_of(scene.geometry->triangles.begin(),scene.geometry->triangles.end(),[&](const auto& t){return t.material==defaultMaterial;}))scene.materials.pop_back();
    return scene;
}
}
