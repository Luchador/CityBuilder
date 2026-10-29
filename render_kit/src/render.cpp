#include "render.hpp"
#include "camera.hpp"
#include <numbers>
#include <thread>
#include <stdexcept>

namespace renderkit
{
namespace
{
    bool Intersects(const Bounds& box, Vec3 origin, Vec3 direction, float maxDistance)
    {
        float near=0,far=maxDistance;
        for(int axis=0;axis<3;++axis)
        {
            const float o=Component(origin,axis),d=Component(direction,axis);
            const float lo=Component(box.min,axis),hi=Component(box.max,axis);
            if(std::abs(d)<1e-20f) {if(o<lo || o>hi)return false;continue;}
            float a=(lo-o)/d,b=(hi-o)/d;
            if(a>b)std::swap(a,b);
            near=std::max(near,a);far=std::min(far,b);
            if(near>far)return false;
        }
        return true;
    }
    Vec2 UvAt(const Triangle& t, const Material& material, float u, float v)
    {
        const float w=1-u-v;const auto coord=material.texcoord;
        Vec2 uv{t.vertices[0].uv[coord].x*w+t.vertices[1].uv[coord].x*u+t.vertices[2].uv[coord].x*v,
                t.vertices[0].uv[coord].y*w+t.vertices[1].uv[coord].y*u+t.vertices[2].uv[coord].y*v};
        uv.x*=material.uvScale.x;uv.y*=material.uvScale.y;
        const float c=std::cos(material.uvRotation),s=std::sin(material.uvRotation);
        return {c*uv.x-s*uv.y+material.uvOffset.x,s*uv.x+c*uv.y+material.uvOffset.y};
    }
    Vec4 ColorAt(const Triangle& t, float u, float v)
    {
        const auto a=t.vertices[0].color,b=t.vertices[1].color,c=t.vertices[2].color;
        const float w=1-u-v;
        return {a.r*w+b.r*u+c.r*v,a.g*w+b.g*u+c.g*v,a.b*w+b.b*u+c.b*v,a.a*w+b.a*u+c.a*v};
    }
    bool Accepts(const Triangle& triangle, const Material& material, float u, float v)
    {
        if(material.alphaMode==AlphaMode::Opaque)return true;
        const Vec2 uv=UvAt(triangle,material,u,v);
        float alpha=material.baseFactor.a*ColorAt(triangle,u,v).a;
        if(material.baseColor && material.useBaseColorAlpha)alpha*=material.baseColor->Sample(uv,material.wrapU,material.wrapV).a;
        if(material.opacity)
        {
            const auto& vertices=triangle.vertices;
            const Vec2 opacityUv=material.separateOpacityUv ? Vec2{
                vertices[0].uv[1].x*(1-u-v)+vertices[1].uv[1].x*u+vertices[2].uv[1].x*v,
                vertices[0].uv[1].y*(1-u-v)+vertices[1].uv[1].y*u+vertices[2].uv[1].y*v} : uv;
            const Vec4 opacity=material.opacity->Sample(opacityUv,
                material.separateOpacityUv?material.opacityWrapU:material.wrapU,
                material.separateOpacityUv?material.opacityWrapV:material.wrapV);
            float mask=material.opacityChannel==OpacityChannel::Alpha?opacity.a:
                   material.opacityChannel==OpacityChannel::Red?opacity.r:
                   .2126f*opacity.r+.7152f*opacity.g+.0722f*opacity.b;
            mask=std::clamp(mask*material.opacityStrength,0.0f,1.0f);
            alpha*=material.invertOpacity?1-mask:mask;
        }
        return alpha>=material.cutoff;
    }
    std::uint8_t Byte(float value)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0f,1.0f)*255));
    }
    Vec3 LinearColor(Vec3 color)
    {
        return {SrgbToLinear(std::clamp(color.x,0.0f,1.0f)),SrgbToLinear(std::clamp(color.y,0.0f,1.0f)),
                SrgbToLinear(std::clamp(color.z,0.0f,1.0f))};
    }
    Vec3 AlbedoAt(const Triangle& triangle,const Material& material,float u,float v,float visibility)
    {
        const Vec4 texture=material.baseColor?material.baseColor->Sample(UvAt(triangle,material,u,v),material.wrapU,material.wrapV,true):Vec4{};
        const Vec4 color=ColorAt(triangle,u,v);
        return {LinearToSrgb(texture.r*material.baseFactor.r*color.r*visibility),
                LinearToSrgb(texture.g*material.baseFactor.g*color.g*visibility),
                LinearToSrgb(texture.b*material.baseFactor.b*color.b*visibility)};
    }
    std::vector<Vec3> OcclusionSamples(const AmbientOcclusion& ao)
    {
        if(!ao.enabled || ao.intensity==0)return {};
        std::vector<Vec3> samples;samples.reserve(ao.samples);
        // Deterministic Hammersley points mapped onto a cosine-weighted hemisphere.
        // No pixel/frame random seed: the same surface sees the same rays in all views.
        for(int i=0;i<ao.samples;++i)
        {
            float inverse=0,weight=.5f;
            for(auto bits=static_cast<unsigned>(i);bits;bits>>=1,weight*=.5f)
                if(bits&1u)inverse+=weight;
            const float u=(i+.5f)/ao.samples;
            const float radius=std::sqrt(u),angle=2*std::numbers::pi_v<float>*inverse;
            samples.push_back({radius*std::cos(angle),radius*std::sin(angle),std::sqrt(1-u)});
        }
        return samples;
    }
    float OcclusionVisibility(const Scene& scene,const Hit& hit,Vec3 normal,Vec3 cameraRay,
                              const AmbientOcclusion& ao,const std::vector<Vec3>& samples,
                              float bias,std::stop_token stop)
    {
        if(samples.empty())return 1;
        const auto& v=scene.geometry->triangles[hit.triangle].vertices;
        Vec3 geometric=Normalize(Cross(v[1].position-v[0].position,v[2].position-v[0].position));
        // The visible side of a two-sided leaf must sample its outward hemisphere.
        if(Dot(geometric,cameraRay)>0)geometric=geometric*-1.0f;
        if(Dot(normal,geometric)<=0)normal=geometric;
        const Vec3 tangent=Normalize(Cross(std::abs(normal.y)<.99f?Vec3{0,1,0}:Vec3{1,0,0},normal));
        const Vec3 bitangent=Cross(normal,tangent);
        const Vec3 point=v[0].position*(1-hit.u-hit.v)+v[1].position*hit.u+v[2].position*hit.v;
        const Vec3 origin=point+geometric*std::min(bias,ao.radius*.001f);
        int blocked=0;
        for(const auto sample:samples)
        {
            if(stop.stop_requested())return 1;
            const Vec3 ray=tangent*sample.x+bitangent*sample.y+normal*sample.z;
            // Smooth shading normals can aim rays inside their own polygon. Do not
            // let those rays create dark seams on otherwise exposed convex surfaces.
            if(Dot(ray,geometric)<=0)continue;
            // The infinite ground plane uses the same height as FitFrame's pivot.
            // Test it only here: it never affects camera hits, framing or sun rays.
            bool groundHit=false;
            if(ao.useGroundPlane && ray.y<0)
            {
                const float distance=(scene.geometry->bounds.min.y-origin.y)/ray.y;
                groundHit=distance>=0 && distance<ao.radius;
            }
            // Count each sample once, even if it hits both the ground and model.
            // Model occluders still use the PNG/cutoff test and two-sided traversal.
            if(groundHit || Trace(scene,origin,ray,ao.radius,true,hit.triangle).found)++blocked;
        }
        return 1-ao.intensity*static_cast<float>(blocked)/static_cast<float>(samples.size());
    }
}
void RenderSettings::Validate() const
{
    if(width<16 || height<16 || width>4096 || height>4096)
        throw std::invalid_argument("Output width and height must be 16..4096 pixels");
    if(aaGridSize!=1 && aaGridSize!=2 && aaGridSize!=4 && aaGridSize!=8)
        throw std::invalid_argument("Anti-aliasing grid size must be 1, 2, 4 or 8");
    if(!std::isfinite(padding) || padding<0 || padding>.45f || !std::isfinite(pixelsPerUnit) || pixelsPerUnit<=0 || pixelsPerUnit>4096)
        throw std::invalid_argument("Invalid framing settings");
    const auto color=[](Vec3 c){return Finite(c) && c.x>=0 && c.y>=0 && c.z>=0 && c.x<=1 && c.y<=1 && c.z<=1;};
    if(!color(lighting.ambientColor) || !color(lighting.directionalColor) ||
       !std::isfinite(lighting.ambientIntensity) || lighting.ambientIntensity<0 || lighting.ambientIntensity>10 ||
       !std::isfinite(lighting.directionalIntensity) || lighting.directionalIntensity<0 || lighting.directionalIntensity>10)
        throw std::invalid_argument("Invalid lighting settings");
    const auto& ao=ambientOcclusion;
    if(!std::isfinite(ao.intensity) || ao.intensity<0 || ao.intensity>1 ||
       !std::isfinite(ao.radius) || ao.radius<.001f || ao.radius>10000 || ao.samples<16 || ao.samples>256)
        throw std::invalid_argument("Invalid ambient occlusion settings: intensity 0..1, radius 0.001..10000, samples 16..256");
}
Basis ViewBasis(unsigned quarterTurns)
{
    const auto c=GetCameraOrientation(quarterTurns);
    return {{c.cosYaw,0,-c.sinYaw},
            {-c.sinPitch*c.sinYaw,c.cosPitch,-c.sinPitch*c.cosYaw},
            {c.cosPitch*c.sinYaw,c.sinPitch,c.cosPitch*c.cosYaw}};
}
Frame FitFrame(const Geometry& geometry,const RenderSettings& settings)
{
    settings.Validate();
    if(geometry.triangles.empty())throw std::invalid_argument("No model loaded");
    Frame frame;frame.center=geometry.bounds.Center();
    frame.pivot={frame.center.x,geometry.bounds.min.y,frame.center.z};
    const float radius=Length(geometry.bounds.max-geometry.bounds.min)*.5f;
    if(!std::isfinite(radius) || radius<1e-8f)throw std::runtime_error("Model has no usable extent");
    frame.distance=radius*3.0f+1.0f;
    if(settings.autoFit)
    {
        float halfWidth=0,halfHeight=0;
        for(unsigned angle=0;angle<4;++angle)
        {
            const auto basis=ViewBasis(angle);
            for(const auto& t:geometry.triangles)for(const auto& vertex:t.vertices)
            {
                const Vec3 p=vertex.position-frame.center;
                halfWidth=std::max(halfWidth,std::abs(Dot(p,basis.right)));
                halfHeight=std::max(halfHeight,std::abs(Dot(p,basis.up)));
            }
        }
        const float aspect=static_cast<float>(settings.width)/settings.height;
        frame.halfHeight=std::max(halfHeight,halfWidth/aspect)/std::max(.1f,1-2*settings.padding);
    }
    else frame.halfHeight=settings.height/(2*settings.pixelsPerUnit);
    frame.pixelsPerUnit=settings.height/(2*frame.halfHeight);
    for(unsigned angle=0;angle<4;++angle)
    {
        const auto b=ViewBasis(angle);const Vec3 p=frame.pivot-frame.center;
        frame.pivotPixels[angle]={settings.width*.5f+Dot(p,b.right)*frame.pixelsPerUnit,
                                 settings.height*.5f-Dot(p,b.up)*frame.pixelsPerUnit};
    }
    return frame;
}
Hit Trace(const Scene& scene,Vec3 origin,Vec3 direction,float maxDistance,bool shadowRay,std::uint32_t ignoredTriangle)
{
    Hit closest;closest.distance=maxDistance;
    const auto& geometry=*scene.geometry;
    if(geometry.nodes.empty())return closest;
    // Median splits limit depth to < 24 for the supported triangle limit.
    std::array<std::uint32_t,64> stack{};std::size_t size=1;stack[0]=0;
    while(size)
    {
        const auto& node=geometry.nodes[stack[--size]];
        if(!Intersects(node.bounds,origin,direction,closest.distance))continue;
        if(!node.count) {stack[size++]=node.left;stack[size++]=node.right;continue;}
        for(std::uint32_t i=node.first;i<node.first+node.count;++i)
        {
            const auto index=geometry.order[i];const auto& triangle=geometry.triangles[index];
            if(index==ignoredTriangle)continue;
            const auto& material=scene.materials[triangle.material];
            const auto& p=triangle.vertices;
            const Vec3 e1=p[1].position-p[0].position,e2=p[2].position-p[0].position;
            const Vec3 h=Cross(direction,e2);const float determinant=Dot(e1,h);
            const float epsilon=1e-7f*Length(e1)*Length(e2);
            if(std::abs(determinant)<=epsilon || (!shadowRay && !material.doubleSided && determinant<=epsilon))continue;
            const float inverse=1/determinant;const Vec3 s=origin-p[0].position;
            const float u=Dot(s,h)*inverse;if(u<0 || u>1)continue;
            const Vec3 q=Cross(s,e1);const float v=Dot(direction,q)*inverse;if(v<0 || u+v>1)continue;
            const float distance=Dot(e2,q)*inverse;
            if(distance<=0 || distance>=closest.distance || !Accepts(triangle,material,u,v))continue;
            closest={true,distance,index,u,v};
            if(shadowRay)return closest;
        }
    }
    return closest;
}
RenderImage Render(const Scene& scene,const RenderSettings& settings,const Frame& frame,
                   unsigned quarterTurns,std::stop_token stop,std::atomic<int>* completedRows)
{
    settings.Validate();
    if(!scene.geometry || scene.geometry->nodes.empty())throw std::invalid_argument("No model loaded");
    if(stop.stop_requested())throw std::runtime_error("Render cancelled");
    RenderImage result;result.width=settings.width;result.height=settings.height;
    const auto bytes=static_cast<std::size_t>(settings.width)*settings.height*4;
    result.rgba.resize(bytes);result.albedo.resize(bytes);result.directional.resize(bytes);
    const auto basis=ViewBasis(quarterTurns);
    const Vec3 direction=basis.towardCamera*-1.0f;
    const Vec3 lightRay=Normalize(DirectionalLightDirection);
    const float worldPerPixel=2*frame.halfHeight/settings.height;
    const float shadowBias=std::max(1e-6f,Length(scene.geometry->bounds.max-scene.geometry->bounds.min)*1e-5f);
    const auto& light=settings.lighting;
    const auto samples=OcclusionSamples(settings.ambientOcclusion);
    const int grid=settings.aaGridSize,sampleCount=grid*grid;
    std::atomic<int> nextRow{0};
    const auto work=[&]
    {
        for(int y=nextRow.fetch_add(1);y<settings.height && !stop.stop_requested();y=nextRow.fetch_add(1))
        {
            for(int x=0;x<settings.width;++x)
            {
                const auto index=(static_cast<std::size_t>(y)*settings.width+x)*4;
                Vec3 litSum{},albedoSum{};float sunSum=0;int covered=0;
                // Deterministic subpixel centers. Accumulate locally rather than
                // allocating a supersized image; output dimensions stay unchanged.
                for(int sy=0;sy<grid;++sy)for(int sx=0;sx<grid;++sx)
                {
                    if(stop.stop_requested())return;
                    const Vec3 origin=frame.center+basis.towardCamera*frame.distance+
                        basis.right*((x+(sx+.5f)/grid-settings.width*.5f)*worldPerPixel)+
                        basis.up*((settings.height*.5f-y-(sy+.5f)/grid)*worldPerPixel);
                    const Hit hit=Trace(scene,origin,direction,frame.distance*2);
                    if(!hit.found)continue;
                    const auto& triangle=scene.geometry->triangles[hit.triangle];const auto& material=scene.materials[triangle.material];
                    const auto& vertices=triangle.vertices;
                    Vec3 normal=Normalize(vertices[0].normal*(1-hit.u-hit.v)+vertices[1].normal*hit.u+vertices[2].normal*hit.v);
                    if(material.doubleSided && Dot(normal,direction)>0)normal=normal*-1.0f;
                    float sun=std::max(0.0f,Dot(normal,DirectionalLightDirection));
                    if(light.shadows && sun>0)
                    {
                        const Vec3 point=origin+direction*hit.distance;
                        if(Trace(scene,point+normal*shadowBias+lightRay*shadowBias,lightRay,frame.distance*3,true).found)sun=0;
                    }
                    const float visibility=OcclusionVisibility(scene,hit,normal,direction,
                        settings.ambientOcclusion,samples,shadowBias,stop);
                    const Vec3 albedo=AlbedoAt(triangle,material,hit.u,hit.v,visibility);
                    const Vec3 illumination=(light.ambientColor*light.ambientIntensity+light.directionalColor*(light.directionalIntensity*sun))/LightingDivisor;
                    const Vec3 lit{albedo.x*illumination.x,albedo.y*illumination.y,albedo.z*illumination.z};
                    if(grid==1)
                    {
                        // Preserve the original single-sample pixels exactly.
                        result.rgba[index]=Byte(lit.x);result.rgba[index+1]=Byte(lit.y);result.rgba[index+2]=Byte(lit.z);
                        result.albedo[index]=Byte(albedo.x);result.albedo[index+1]=Byte(albedo.y);result.albedo[index+2]=Byte(albedo.z);
                        result.directional[index]=result.directional[index+1]=result.directional[index+2]=Byte(sun);
                        result.rgba[index+3]=result.albedo[index+3]=result.directional[index+3]=255;
                    }
                    else
                    {
                        litSum=litSum+LinearColor(lit);albedoSum=albedoSum+LinearColor(albedo);
                        sunSum+=std::clamp(sun,0.0f,1.0f);++covered;
                    }
                }
                if(grid>1 && covered)
                {
                    // Average covered colors only, then store coverage separately.
                    // This is a premultiplied resolve converted to straight alpha:
                    // transparent misses never darken edge RGB. The sun mask is linear.
                    for(int c=0;c<3;++c)
                    {
                        result.rgba[index+c]=Byte(LinearToSrgb(Component(litSum,c)/covered));
                        result.albedo[index+c]=Byte(LinearToSrgb(Component(albedoSum,c)/covered));
                        result.directional[index+c]=Byte(sunSum/covered);
                    }
                    const auto alpha=Byte(static_cast<float>(covered)/sampleCount);
                    result.rgba[index+3]=result.albedo[index+3]=result.directional[index+3]=alpha;
                }
            }
            if(completedRows)completedRows->fetch_add(1,std::memory_order_relaxed);
        }
    };
    const auto count=std::min(16u,std::max(1u,std::thread::hardware_concurrency()));
    std::vector<std::jthread> workers;workers.reserve(count-1);
    for(unsigned i=1;i<count;++i)workers.emplace_back(work);
    work();
    for(auto& worker:workers)worker.join();
    if(stop.stop_requested())throw std::runtime_error("Render cancelled");
    return result;
}
}
