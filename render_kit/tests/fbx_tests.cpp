#include "export.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace rk=renderkit;
namespace
{
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    bool Near(float a,float b){return std::abs(a-b)<.0001f;}
    template<class Action>void Reject(Action action)
    { bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid FBX was accepted"); }
    void Replace(std::string& text,const std::string& from,const std::string& to)
    {const auto pos=text.find(from);Check(pos!=std::string::npos,"Test fixture replacement missing");text.replace(pos,from.size(),to);}
    void Write(const std::filesystem::path& path,const std::string& text)
    {std::ofstream file(path,std::ios::binary);file<<text;}
    std::string Fixture()
    {
        // Two quads, shared geometry instanced with different materials, a mirrored
        // child and a geometry-only transform. Authored here, independent of a DCC.
        return R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: {
 FBXHeaderVersion: 1003
 FBXVersion: 7400
 Creator: "Render Kit tests"
}
GlobalSettings: {
 Properties70: {
  P: "UpAxis", "int", "Integer", "",1
  P: "UpAxisSign", "int", "Integer", "",1
  P: "FrontAxis", "int", "Integer", "",2
  P: "FrontAxisSign", "int", "Integer", "",1
  P: "CoordAxis", "int", "Integer", "",0
  P: "CoordAxisSign", "int", "Integer", "",1
  P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Objects: {
 Geometry: 100, "Geometry::Cards", "Mesh" {
  Vertices: *18 { a: 0,0,0,2,0,1,2,2,1,0,2,0,4,0,2,4,2,2 }
  PolygonVertexIndex: *8 { a: 0,1,2,-4,1,4,5,-3 }
  LayerElementNormal: 0 {
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   Normals: *24 { a: -0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272,-0.4472136,0,0.8944272 }
  }
  LayerElementUV: 0 {
   Name: "base"
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *16 { a: 0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25,0.25 }
  }
  LayerElementUV: 1 {
   Name: "mask"
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *16 { a: 0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75,0.75 }
  }
  LayerElementColor: 0 {
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   Colors: *32 { a: 0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1,0.5,1,0.75,1 }
  }
  LayerElementMaterial: 0 {
   MappingInformationType: "ByPolygon"
   ReferenceInformationType: "IndexToDirect"
   Materials: *2 { a: 0,1 }
  }
  Layer: 0 {
   LayerElement: { Type: "LayerElementNormal"
    TypedIndex: 0 }
   LayerElement: { Type: "LayerElementUV"
    TypedIndex: 0 }
   LayerElement: { Type: "LayerElementColor"
    TypedIndex: 0 }
   LayerElement: { Type: "LayerElementMaterial"
    TypedIndex: 0 }
  }
  Layer: 1 {
   LayerElement: { Type: "LayerElementUV"
    TypedIndex: 1 }
  }
 }
 Model: 200, "Model::Parent", "Null" {
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",7,2,-3 }
 }
 Model: 201, "Model::Mirrored", "Mesh" {
  Properties70: {
   P: "Lcl Scaling", "Lcl Scaling", "", "A",-2,3,0.5
   P: "GeometricTranslation", "Vector3D", "Vector", "",1,0,0
  }
 }
 Model: 202, "Model::Instance", "Mesh" {
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",20,0,0 }
 }
 Material: 300, "Material::Leaves", "" {
  ShadingModel: "phong"
  Properties70: {
   P: "DiffuseColor", "Color", "", "A",1,1,1
   P: "DiffuseFactor", "Number", "", "A",1
   P: "TransparencyFactor", "Number", "", "A",1
  }
 }
 Material: 301, "Material::Bark", "" {
  ShadingModel: "phong"
  Properties70: { P: "DiffuseColor", "Color", "", "A",0.2,0.3,0.1 }
 }
 Texture: 400, "Texture::Leaf PNG", "" {
  Type: "TextureVideoClip"
  FileName: "C:/old/path/leaf.png"
  RelativeFilename: "textures\leaf.png"
  Properties70: { P: "UVSet", "KString", "", "", "base" }
 }
 Texture: 401, "Texture::Mask PNG", "" {
  Type: "TextureVideoClip"
  FileName: "mask.png"
  RelativeFilename: "mask.png"
  Properties70: { P: "UVSet", "KString", "", "", "mask" }
 }
}
Connections: {
 C: "OO",200,0
 C: "OO",201,200
 C: "OO",202,0
 C: "OO",100,201
 C: "OO",100,202
 C: "OO",300,201
 C: "OO",301,201
 C: "OO",301,202
 C: "OO",300,202
 C: "OP",400,300,"DiffuseColor"
 C: "OP",400,300,"TransparentColor"
}
)FBX";
    }
}

void FbxTests(const std::filesystem::path& folder)
{
    std::filesystem::create_directory(folder/"textures");
    const std::vector<std::uint8_t> pixels={255,0,0,0,0,255,0,255,0,0,255,192,255,255,255,255};
    rk::SavePng(folder/"textures/leaf.png",2,2,pixels);
    rk::SavePng(folder/"mask.png",2,2,{pixels.data(),pixels.size()});
    const auto path=folder/rk::NativePath("tree_é.FBX");auto source=Fixture();Write(path,source);
    const auto scene=rk::LoadModel(path);
    Check(scene.geometry->triangles.size()==8,"FBX quad triangulation/instancing failed");
    Check(scene.materials.size()==2 && scene.materials[0].name=="Leaves" && scene.materials[1].name=="Bark","FBX material names lost");
    Check(Near(scene.geometry->bounds.min.x,-3) && Near(scene.geometry->bounds.max.x,24),"FBX hierarchy/geometric transform incorrect");
    Check(Near(scene.geometry->bounds.max.y,8) && Near(scene.geometry->bounds.min.z,-3),"FBX units/axes incorrect");
    std::size_t leafCount=0;
    for(const auto& triangle:scene.geometry->triangles)
    {
        const auto& v=triangle.vertices;
        const auto face=rk::Normalize(rk::Cross(v[1].position-v[0].position,v[2].position-v[0].position));
        Check(rk::Dot(face,v[0].normal)>.9999f,"FBX mirrored winding/normals incorrect");
        if(v[0].position.x<10)Check(Near(v[0].normal.x,.1240347f),"FBX nonuniform inverse-transpose normal incorrect");
        Check(Near(v[0].color.r,.5f) && Near(v[0].color.b,.75f),"FBX vertex colors lost");
        Check(Near(v[0].uv[0].x,.25f) && Near(v[0].uv[0].y,.75f),"FBX PNG UV flip incorrect");
        if(triangle.material==0){++leafCount;Check(v[0].position.x<10 || v[0].position.x>=22,"Per-instance material assignment ignored");}
    }
    Check(leafCount==4,"FBX per-face material assignment incorrect");
    const auto& leaves=scene.materials[0];
    Check(leaves.baseColor && leaves.baseColor->rgba==pixels,"FBX portable PNG path did not load");
    Check(leaves.alphaMode==rk::AlphaMode::Cutout && leaves.doubleSided,"FBX leaf alpha not detected");
    Check(!leaves.opacity && leaves.useBaseColorAlpha,"Shared leaf alpha applied twice");
    Check(scene.materials[1].alphaMode==rk::AlphaMode::Opaque,"Bark should be opaque");
    // An alpha of 192/255 survives cutoff .6 once, but fails if erroneously squared.
    auto cutout=scene;cutout.materials[0].cutoff=.6f;
    Check(rk::Trace(cutout,{4,3,0},{0,0,-1},10).found,"Shared diffuse/opacity alpha was squared");

    auto meters=source;Replace(meters,"\"\",100","\"\",1");Write(path,meters);
    Check(Near(rk::LoadModel(path).geometry->bounds.max.x,.24f),"FBX centimeters were not normalized");
    auto zup=source;Replace(zup,"\"UpAxis\", \"int\", \"Integer\", \"\",1","\"UpAxis\", \"int\", \"Integer\", \"\",2");
    Replace(zup,"\"FrontAxis\", \"int\", \"Integer\", \"\",2","\"FrontAxis\", \"int\", \"Integer\", \"\",1");
    Replace(zup,"\"FrontAxisSign\", \"int\", \"Integer\", \"\",1","\"FrontAxisSign\", \"int\", \"Integer\", \"\",-1");Write(path,zup);
    const auto upright=rk::LoadModel(path);
    Check(Near(upright.geometry->bounds.min.y,-3) && Near(upright.geometry->bounds.max.y,2),"FBX Z-up conversion failed");
    auto left=source;Replace(left,"\"FrontAxisSign\", \"int\", \"Integer\", \"\",1","\"FrontAxisSign\", \"int\", \"Integer\", \"\",-1");Write(path,left);
    const auto leftScene=rk::LoadModel(path);
    for(const auto& t:leftScene.geometry->triangles)
        Check(rk::Dot(rk::Normalize(rk::Cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position)),t.vertices[0].normal)>.9999f,"FBX handedness conversion broke culling");

    auto separate=source;Replace(separate,"C: \"OP\",400,300,\"TransparentColor\"","C: \"OP\",401,300,\"TransparentColor\"");Write(path,separate);
    auto masked=rk::LoadModel(path);
    Check(masked.materials[0].opacity && masked.materials[0].invertOpacity && !masked.materials[0].useBaseColorAlpha,"Legacy FBX transparency not mapped");
    for(const auto& t:masked.geometry->triangles)if(t.material==0)
        Check(Near(t.vertices[0].uv[1].x,.75f) && Near(t.vertices[0].uv[1].y,.25f),"Separate opacity UV set ignored");
    // Mask sample is green (.7152 luminance): inversion makes it fail a .5 cutoff.
    Check(!rk::Trace(masked,{4,3,0},{0,0,-1},10).found,"Inverted opacity/independent UVs failed");
    masked.materials[0].invertOpacity=false;
    Check(rk::Trace(masked,{4,3,0},{0,0,-1},10).found,"Opacity inversion toggle failed");

    auto missing=source;Replace(missing,"textures\\leaf.png","missing.png");Replace(missing,"C:/old/path/leaf.png","C:/old/path/missing.png");Write(path,missing);
    const auto noTexture=rk::LoadModel(path);Check(!noTexture.materials[0].baseColor && !noTexture.warnings.empty(),"Missing PNG should allow manual assignment");
    Write(path,"not an FBX");Reject([&]{rk::LoadModel(path);});Reject([&]{rk::LoadModel(folder/"tree.obj");});
    Write(path,source);

    // Real binary/ASCII exporter fixtures from our pinned dependency, not just hand-written FBX.
    const auto data=rk::NativePath(RENDER_KIT_FBX_TEST_DATA);
    const auto ascii=rk::LoadModel(data/"maya_cube_7500_ascii.fbx");
    const auto binary=rk::LoadModel(data/"maya_cube_7500_binary.fbx");
    const auto embedded=rk::LoadModel(data/"blender_293_embedded_textures_7400_binary.fbx");
    Check(std::any_of(embedded.materials.begin(),embedded.materials.end(),[](const auto& m){return m.baseColor!=nullptr;}),"Embedded FBX PNG missing");
    const auto generated=rk::LoadModel(data/"synthetic_missing_normals_7400_ascii.fbx");
    Check(!generated.geometry->triangles.empty(),"FBX missing-normal generation failed");
    for(const auto& t:generated.geometry->triangles)for(const auto& v:t.vertices)
        Check(rk::Finite(v.normal) && Near(rk::Length(v.normal),1),"Generated FBX normal invalid");
    Check(binary.geometry->triangles.size()==12 && ascii.geometry->triangles.size()==12,"Binary FBX cube import failed");
    rk::RenderSettings settings;settings.width=64;settings.height=96;
    for(unsigned view=0;view<4;++view)
    {
        const auto a=rk::Render(ascii,settings,rk::FitFrame(*ascii.geometry,settings),view);
        const auto b=rk::Render(binary,settings,rk::FitFrame(*binary.geometry,settings),view);
        Check(a.rgba==b.rgba,"Binary/ASCII FBX renders differ");
        std::size_t visible=0;
        for(std::size_t i=3;i<b.rgba.size();i+=4){Check(b.rgba[i]==0 || b.rgba[i]==255,"FBX export alpha is not binary");visible+=b.rgba[i]!=0;}
        Check(visible>100,"FBX cube did not render");
    }
    Reject([&]{rk::LoadModel(data/"maya_blend_shape_cube_7700_binary.fbx");});
    const auto exported=rk::ExportViews(scene,settings,folder,"fbx",true);
    Check(std::filesystem::exists(exported/"fbx_view3.png") && std::filesystem::exists(exported/"sprites.json"),"FBX four-view export failed");
    std::cout<<"PASS: binary/ASCII FBX, Unicode, axes/units, instances/materials, mirrored normals, UVs, alpha and four-view export\n";
}
