#include "scene.hpp"
#include <SDL3/SDL.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace renderkit
{
std::string Utf8Path(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
std::filesystem::path NativePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
namespace
{
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Cannot open " + Utf8Path(path));
        const auto length = stream.tellg();
        if (length <= 0 || length > 512ll * 1024 * 1024)
            throw std::runtime_error("File is empty or exceeds 512 MiB: " + Utf8Path(path));
        std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
            throw std::runtime_error("Cannot read " + Utf8Path(path));
        return data;
    }
    std::shared_ptr<const Image> ReadSurface(SDL_Surface* loaded, const std::string& name)
    {
        Surface original(loaded, SDL_DestroySurface);
        if (!original) throw std::runtime_error("Load PNG '" + name + "': " + SDL_GetError());
        if (original->w <= 0 || original->h <= 0 || original->w > 8192 || original->h > 8192)
            throw std::runtime_error("PNG dimensions must be 1..8192: " + name);
        Surface surface(SDL_ConvertSurface(original.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!surface) throw std::runtime_error(SDL_GetError());
        auto image = std::make_shared<Image>();
        image->width = surface->w; image->height = surface->h; image->name = name;
        image->rgba.resize(static_cast<std::size_t>(image->width) * image->height * 4);
        if (!SDL_LockSurface(surface.get())) throw std::runtime_error(SDL_GetError());
        for (int y=0; y<image->height; ++y)
            std::memcpy(image->rgba.data() + static_cast<std::size_t>(y)*image->width*4,
                        static_cast<const std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y)*surface->pitch,
                        static_cast<std::size_t>(image->width)*4);
        SDL_UnlockSurface(surface.get());
        return image;
    }
    int WrapPixel(int p, int size, Wrap wrap)
    {
        if (wrap == Wrap::Clamp) return std::clamp(p,0,size-1);
        const int period = wrap == Wrap::Mirror ? size*2 : size;
        p = ((p%period)+period)%period;
        return p < size ? p : period-1-p;
    }
    float WrapUv(float u, Wrap mode)
    {
        if (mode == Wrap::Clamp) return std::clamp(u,0.0f,1.0f);
        const float period = mode == Wrap::Mirror ? 2.0f : 1.0f;
        return u-std::floor(u/period)*period;
    }
    cgltf_result ReadGltfFile(const cgltf_memory_options*, const cgltf_file_options*,
                             const char* path, cgltf_size* size, void** output)
    {
        try
        {
            auto data = ReadBytes(NativePath(path));
            if (*size && *size > data.size()) return cgltf_result_data_too_short;
            *size = data.size();
            *output = std::malloc(*size);
            if (!*output) return cgltf_result_out_of_memory;
            std::memcpy(*output,data.data(),data.size());
            return cgltf_result_success;
        }
        catch (...) { return cgltf_result_io_error; }
    }
    void ReleaseGltfFile(const cgltf_memory_options*, const cgltf_file_options*, void* data, cgltf_size)
    {
        std::free(data);
    }
    void Require(cgltf_result result, const char* action)
    {
        if (result != cgltf_result_success)
            throw std::runtime_error(std::string(action) + " (glTF error " + std::to_string(static_cast<int>(result)) + ")");
    }
    Vec3 TransformPosition(const float* m, Vec3 p)
    {
        return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    }
    std::vector<std::uint8_t> DecodeBase64(std::string_view text)
    {
        constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<std::uint8_t> out;
        std::uint32_t value=0; int bits=0;
        for (char c : text)
        {
            if(c=='=') break;
            const auto digit=alphabet.find(c);
            if(digit==std::string_view::npos) throw std::runtime_error("Invalid image data URI");
            value=(value<<6)|static_cast<std::uint32_t>(digit); bits+=6;
            if(bits>=8) { bits-=8; out.push_back(static_cast<std::uint8_t>(value>>bits)); }
        }
        return out;
    }
    Wrap TextureWrap(cgltf_wrap_mode mode)
    {
        return mode==cgltf_wrap_mode_clamp_to_edge ? Wrap::Clamp : mode==cgltf_wrap_mode_mirrored_repeat ? Wrap::Mirror : Wrap::Repeat;
    }
}

Vec4 Image::Sample(Vec2 uv, Wrap u, Wrap v, bool linearRgb) const
{
    static const auto linear=[]
    {
        std::array<float,256> values{};
        for(std::size_t i=0;i<values.size();++i)values[i]=SrgbToLinear(static_cast<float>(i)/255.0f);
        return values;
    }();
    const float px=WrapUv(uv.x,u)*width-.5f, py=WrapUv(uv.y,v)*height-.5f;
    const int x=static_cast<int>(std::floor(px)), y=static_cast<int>(std::floor(py));
    const float tx=px-std::floor(px), ty=py-std::floor(py);
    float channels[4]{};
    for(int j=0;j<2;++j) for(int i=0;i<2;++i)
    {
        const auto index=(static_cast<std::size_t>(WrapPixel(y+j,height,v))*width+WrapPixel(x+i,width,u))*4;
        const float weight=(i?tx:1-tx)*(j?ty:1-ty);
        for(int c=0;c<4;++c) channels[c]+=(linearRgb && c<3?linear[rgba[index+c]]:rgba[index+c]/255.0f)*weight;
    }
    return {channels[0],channels[1],channels[2],channels[3]};
}
std::shared_ptr<const Image> LoadPng(const std::filesystem::path& path)
{
    return ReadSurface(SDL_LoadPNG(Utf8Path(path).c_str()),Utf8Path(path));
}
std::shared_ptr<const Image> DecodePng(std::span<const std::uint8_t> bytes, const std::string& name)
{
    auto* io=SDL_IOFromConstMem(bytes.data(),bytes.size());
    if(!io) throw std::runtime_error(SDL_GetError());
    return ReadSurface(SDL_LoadPNG_IO(io,true),name);
}
void SavePng(const std::filesystem::path& path, int width, int height, std::span<const std::uint8_t> rgba)
{
    if(width<1 || height<1 || width>4096 || height>4096 || rgba.size()!=static_cast<std::size_t>(width)*height*4)
        throw std::invalid_argument("Invalid output image dimensions");
    Surface surface(SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,
                    const_cast<std::uint8_t*>(rgba.data()),width*4),SDL_DestroySurface);
    if(!surface || !SDL_SavePNG(surface.get(),Utf8Path(path).c_str()))
        throw std::runtime_error("Save PNG '"+Utf8Path(path)+"': "+SDL_GetError());
}

void Geometry::BuildAcceleration()
{
    if(triangles.empty()) throw std::runtime_error("The model has no renderable triangles");
    if(triangles.size()>5'000'000) throw std::runtime_error("The model exceeds five million triangles");
    bounds={}; nodes.clear(); order.resize(triangles.size());
    std::iota(order.begin(),order.end(),0u);
    for(const auto& triangle:triangles) for(const auto& vertex:triangle.vertices)
    {
        if(!Finite(vertex.position) || !Finite(vertex.normal) ||
           !std::isfinite(vertex.color.r) || !std::isfinite(vertex.color.g) ||
           !std::isfinite(vertex.color.b) || !std::isfinite(vertex.color.a))
            throw std::runtime_error("Model has nonfinite positions, normals or colors");
        bounds.Include(vertex.position);
    }
    nodes.reserve(triangles.size()*2);
    const auto build=[&](auto&& self,std::uint32_t first,std::uint32_t count)->std::uint32_t
    {
        const auto node=static_cast<std::uint32_t>(nodes.size()); nodes.emplace_back();
        Bounds box,centers;
        for(std::uint32_t i=first;i<first+count;++i)
        {
            const auto& t=triangles[order[i]];
            for(const auto& v:t.vertices)box.Include(v.position);
            centers.Include((t.vertices[0].position+t.vertices[1].position+t.vertices[2].position)/3.0f);
        }
        nodes[node].bounds=box;
        if(count<=8) {nodes[node].first=first;nodes[node].count=count;return node;}
        const Vec3 span=centers.max-centers.min;
        const int axis=span.x>=span.y && span.x>=span.z ? 0 : span.y>=span.z ? 1 : 2;
        const auto middle=first+count/2;
        std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](auto a,auto b)
        {
            const auto& x=triangles[a].vertices;const auto& y=triangles[b].vertices;
            return Component(x[0].position+x[1].position+x[2].position,axis)<Component(y[0].position+y[1].position+y[2].position,axis);
        });
        const auto left=self(self,first,count/2),right=self(self,middle,count-count/2);
        nodes[node].left=left;nodes[node].right=right;return node;
    };
    build(build,0,static_cast<std::uint32_t>(triangles.size()));
}

Scene LoadGlb(const std::filesystem::path& path)
{
    const auto bytes=ReadBytes(path);
    cgltf_options options{}; options.type=cgltf_file_type_glb;
    options.file.read=ReadGltfFile;options.file.release=ReleaseGltfFile;
    cgltf_data* raw=nullptr;
    Require(cgltf_parse(&options,bytes.data(),bytes.size(),&raw),"Parse GLB");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    Require(cgltf_load_buffers(&options,data.get(),Utf8Path(path).c_str()),"Load GLB buffers");
    Require(cgltf_validate(data.get()),"Validate GLB");
    for(cgltf_size i=0;i<data->extensions_required_count;++i)
    {
        const std::string extension=data->extensions_required[i];
        if(extension!="KHR_texture_transform" && extension!="KHR_mesh_quantization" && extension!="KHR_materials_unlit")
            throw std::runtime_error("Unsupported required glTF extension: "+extension+". Export an uncompressed static GLB.");
    }
    Scene scene;scene.source=Utf8Path(path);
    if(data->animations_count) scene.warnings.push_back("Animations are not played; rendering the model's default static pose.");
    auto geometry=std::make_shared<Geometry>();
    std::unordered_map<const cgltf_image*,std::shared_ptr<const Image>> images;
    const auto getImage=[&](const cgltf_image* source)->std::shared_ptr<const Image>
    {
        if(!source)return {};
        if(auto it=images.find(source);it!=images.end())return it->second;
        std::shared_ptr<const Image> image;
        const std::string name=source->name?source->name:"Embedded base color";
        try
        {
            if(source->buffer_view)
                image=DecodePng({cgltf_buffer_view_data(source->buffer_view),source->buffer_view->size},name);
            else if(source->uri)
            {
                const std::string uri=source->uri;
                if(uri.starts_with("data:"))
                {
                    const auto comma=uri.find(',');
                    if(comma==std::string::npos || uri.substr(0,comma).find(";base64")==std::string::npos)
                        throw std::runtime_error("Only base64 image data URIs are supported");
                    const auto decoded=DecodeBase64(std::string_view(uri).substr(comma+1));
                    image=DecodePng(decoded,name);
                }
                else
                {
                    if(uri.find("://")!=std::string::npos)throw std::runtime_error("Remote texture URLs are not supported");
                    std::string decoded=uri;decoded.resize(cgltf_decode_uri(decoded.data()));
                    image=LoadPng(path.parent_path()/NativePath(decoded));
                }
            }
        }
        catch(const std::exception& error)
        {
            scene.warnings.push_back(name+": "+error.what()+". Assign a PNG in Materials.");
        }
        images.emplace(source,image);return image;
    };
    for(cgltf_size i=0;i<data->materials_count;++i)
    {
        const auto& input=data->materials[i];Material material;
        material.name=input.name?input.name:"Material "+std::to_string(i+1);
        const auto& pbr=input.pbr_metallic_roughness;
        material.baseFactor={pbr.base_color_factor[0],pbr.base_color_factor[1],pbr.base_color_factor[2],pbr.base_color_factor[3]};
        for(float value:pbr.base_color_factor)
            if(!std::isfinite(value) || value<0 || value>1)throw std::runtime_error("Invalid material base-color factor");
        material.doubleSided=input.double_sided!=0;
        material.alphaMode=input.alpha_mode==cgltf_alpha_mode_opaque?AlphaMode::Opaque:AlphaMode::Cutout;
        material.cutoff=std::clamp(input.alpha_cutoff,0.0f,1.0f);
        if(input.alpha_mode==cgltf_alpha_mode_blend)
            scene.warnings.push_back(material.name+": blended transparency imported as alpha cut-out (binary sprite alpha).");
        if(input.unlit)scene.warnings.push_back(material.name+": unlit flag replaced by Render Kit lighting.");
        const auto& view=pbr.base_color_texture;
        material.texcoord=view.texcoord;
        if(view.has_transform)
        {
            material.uvOffset={view.transform.offset[0],view.transform.offset[1]};
            material.uvScale={view.transform.scale[0],view.transform.scale[1]};
            material.uvRotation=view.transform.rotation;
            if(view.transform.has_texcoord)material.texcoord=view.transform.texcoord;
        }
        if(material.texcoord<0 || material.texcoord>1)throw std::runtime_error("Only TEXCOORD_0 and TEXCOORD_1 are supported");
        if(!std::isfinite(material.uvOffset.x) || !std::isfinite(material.uvOffset.y) ||
           !std::isfinite(material.uvScale.x) || !std::isfinite(material.uvScale.y) ||
           !std::isfinite(material.uvRotation) || !std::isfinite(material.cutoff))
            throw std::runtime_error("Invalid material UV transform or alpha cutoff");
        if(view.texture)
        {
            material.baseColor=getImage(view.texture->image);
            if(view.texture->sampler)
            {
                material.wrapU=TextureWrap(view.texture->sampler->wrap_s);
                material.wrapV=TextureWrap(view.texture->sampler->wrap_t);
            }
        }
        scene.materials.push_back(std::move(material));
    }
    const auto defaultMaterial=static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.emplace_back();
    std::vector<bool> visited(data->nodes_count,false);
    const auto visit=[&](auto&& self,const cgltf_node* node,unsigned depth)->void
    {
        if(depth>256)throw std::runtime_error("Model node hierarchy is too deep");
        const auto nodeIndex=static_cast<std::size_t>(node-data->nodes);
        if(visited[nodeIndex])return;
        visited[nodeIndex]=true;
        const auto* parent=node;
        for(std::size_t hop=0;parent;++hop,parent=parent->parent)
            if(hop>data->nodes_count)throw std::runtime_error("Cycle in model node hierarchy");
        if(node->skin || node->has_mesh_gpu_instancing)
            throw std::runtime_error("Bake skinned/instanced meshes into static geometry before importing");
        float matrix[16];cgltf_node_transform_world(node,matrix);
        const Vec3 a{matrix[0],matrix[1],matrix[2]},b{matrix[4],matrix[5],matrix[6]},c{matrix[8],matrix[9],matrix[10]};
        const float determinant=Dot(a,Cross(b,c));
        if(node->mesh)
        {
            if(!std::isfinite(determinant) || std::abs(determinant)<1e-20f)throw std::runtime_error("Mesh has a singular transform");
            for(cgltf_size p=0;p<node->mesh->primitives_count;++p)
            {
                const auto& primitive=node->mesh->primitives[p];
                if(primitive.has_draco_mesh_compression || primitive.targets_count)
                    throw std::runtime_error("Bake morph targets and disable Draco compression before importing");
                if(primitive.type!=cgltf_primitive_type_triangles && primitive.type!=cgltf_primitive_type_triangle_strip && primitive.type!=cgltf_primitive_type_triangle_fan)
                {scene.warnings.push_back("A line/point primitive was skipped.");continue;}
                const auto* positions=cgltf_find_accessor(&primitive,cgltf_attribute_type_position,0);
                if(!positions || positions->count==0 || positions->type!=cgltf_type_vec3)throw std::runtime_error("Mesh has no valid positions");
                const auto* normals=cgltf_find_accessor(&primitive,cgltf_attribute_type_normal,0);
                const auto* colors=cgltf_find_accessor(&primitive,cgltf_attribute_type_color,0);
                const cgltf_accessor* uvs[2]={cgltf_find_accessor(&primitive,cgltf_attribute_type_texcoord,0),cgltf_find_accessor(&primitive,cgltf_attribute_type_texcoord,1)};
                for(const auto* attribute:{normals,colors,uvs[0],uvs[1]})
                    if(attribute && attribute->count!=positions->count)throw std::runtime_error("Vertex attribute counts do not match");
                if(positions->count>10'000'000)throw std::runtime_error("Mesh exceeds ten million vertices");
                std::vector<Vertex> vertices(positions->count);
                for(cgltf_size i=0;i<positions->count;++i)
                {
                    float value[4]{};
                    if(!cgltf_accessor_read_float(positions,i,value,3))throw std::runtime_error("Cannot read vertex position");
                    auto& vertex=vertices[i];vertex.position=TransformPosition(matrix,{value[0],value[1],value[2]});
                    if(normals)
                    {
                        if(!cgltf_accessor_read_float(normals,i,value,3))throw std::runtime_error("Cannot read vertex normal");
                        if(!Finite({value[0],value[1],value[2]}))throw std::runtime_error("Nonfinite vertex normal");
                        vertex.normal=Normalize((Cross(b,c)*value[0]+Cross(c,a)*value[1]+Cross(a,b)*value[2])/determinant);
                    }
                    for(int uv=0;uv<2;++uv)if(uvs[uv])
                    {
                        if(!cgltf_accessor_read_float(uvs[uv],i,value,2) || !std::isfinite(value[0]) || !std::isfinite(value[1]))
                            throw std::runtime_error("Invalid texture coordinates");
                        vertex.uv[uv]={value[0],value[1]};
                    }
                    if(colors)
                    {
                        value[3]=1;
                        if(!cgltf_accessor_read_float(colors,i,value,4))throw std::runtime_error("Cannot read vertex color");
                        vertex.color={value[0],value[1],value[2],value[3]};
                    }
                }
                const auto material=primitive.material?static_cast<std::uint32_t>(primitive.material-data->materials):defaultMaterial;
                const auto count=primitive.indices?primitive.indices->count:positions->count;
                if(count<3 || (primitive.type==cgltf_primitive_type_triangles && count%3))throw std::runtime_error("Invalid triangle index count");
                const auto index=[&](cgltf_size i)
                {
                    const auto result=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
                    if(result>=vertices.size())throw std::runtime_error("Triangle index is out of bounds");
                    return result;
                };
                const auto append=[&](cgltf_size ia,cgltf_size ib,cgltf_size ic)
                {
                    Triangle triangle{{vertices[index(ia)],vertices[index(ib)],vertices[index(ic)]},material};
                    if(determinant<0)std::swap(triangle.vertices[1],triangle.vertices[2]);
                    const Vec3 face=Cross(triangle.vertices[1].position-triangle.vertices[0].position,triangle.vertices[2].position-triangle.vertices[0].position);
                    if(Length(face)<1e-20f)return;
                    if(!normals)for(auto& vertex:triangle.vertices)vertex.normal=Normalize(face);
                    if(geometry->triangles.size()>=5'000'000)throw std::runtime_error("Model exceeds five million triangles");
                    geometry->triangles.push_back(std::move(triangle));
                };
                if(primitive.type==cgltf_primitive_type_triangles)for(cgltf_size i=0;i<count;i+=3)append(i,i+1,i+2);
                else for(cgltf_size i=2;i<count;++i)
                    if(primitive.type==cgltf_primitive_type_triangle_fan)append(0,i-1,i);
                    else if(i%2)append(i-1,i-2,i);else append(i-2,i-1,i);
            }
        }
        for(cgltf_size i=0;i<node->children_count;++i)self(self,node->children[i],depth+1);
    };
    const auto* active=data->scene?data->scene:(data->scenes_count?&data->scenes[0]:nullptr);
    if(active)for(cgltf_size i=0;i<active->nodes_count;++i)visit(visit,active->nodes[i],0);
    else for(cgltf_size i=0;i<data->nodes_count;++i)if(!data->nodes[i].parent)visit(visit,&data->nodes[i],0);
    geometry->BuildAcceleration();scene.geometry=std::move(geometry);
    // Keep the default only when an unassigned primitive actually uses it.
    if(std::none_of(scene.geometry->triangles.begin(),scene.geometry->triangles.end(),[&](const auto& t){return t.material==defaultMaterial;}))
        scene.materials.pop_back();
    return scene;
}
}
