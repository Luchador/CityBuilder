#include "global_illumination.hpp"
#include <bit>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace renderkit
{
namespace
{
    constexpr float Infinity=std::numeric_limits<float>::infinity();

    Vec3 Multiply(Vec3 a,Vec3 b)
    {
        return {a.x*b.x,a.y*b.y,a.z*b.z};
    }

    Vec3 Linear(Vec3 c)
    {
        return {SrgbToLinear(c.x),SrgbToLinear(c.y),SrgbToLinear(c.z)};
    }

    std::uint8_t Byte(float v)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v,0.0f,1.0f)*255));
    }

    void StoreColor(std::vector<std::uint8_t>& bytes,std::size_t index,Vec3 color)
    {
        for(int c=0;c<3;++c)
        {
            bytes[index+c]=Byte(LinearToSrgb(Component(color,c)));
        }
    }

    std::uint32_t Hash(std::uint32_t v)
    {
        v^=v>>16;
        v*=0x7feb352du;
        v^=v>>15;
        v*=0x846ca68bu;
        return v^(v>>16);
    }

    float Random(std::uint32_t& state)
    {
        state+=0x9e3779b9u;
        return static_cast<float>(Hash(state)>>8)*(1.0f/16777216.0f);
    }

    float RadicalInverse(unsigned bits)
    {
        float value=0,weight=.5f;
        for(;bits;bits>>=1,weight*=.5f)if(bits&1)value+=weight;
        return value;
    }

    Vec3 Hemisphere(Vec3 normal,float u,float v)
    {
        const Vec3 tangent=Normalize(Cross(std::abs(normal.y)<.99f?Vec3{0,1,0}:Vec3{1,0,0},normal));
        const Vec3 bitangent=Cross(normal,tangent);
        const float radius=std::sqrt(u),angle=2*std::numbers::pi_v<float>*v;
        return tangent*(radius*std::cos(angle))+bitangent*(radius*std::sin(angle))+normal*std::sqrt(1-u);
    }

    float Sun(const Scene& scene,const Surface& surface,Vec3 light,bool shadows,float bias)
    {
        if(Dot(surface.geometric,light)<=0)
        {
            return 0;
        }

        const float cosine=std::max(0.0f,Dot(surface.normal,light));

        if(shadows && cosine>0 && Trace(scene,surface.point+surface.geometric*bias,light,Infinity,true,surface.triangle).found)
        {
            return 0;
        }

        return cosine;
    }

    bool NextSurface(const Scene& scene,const Surface& previous,Vec3 ray, const RenderSettings::GlobalIllumination& gi,float bias,Surface& next)
    {
        const Vec3 origin=previous.point+previous.geometric*bias;

        // Secondary rays need the NEAREST accepted hit, including backs of opaque cards.
        const auto hit=Trace(scene,origin,ray,Infinity,false,previous.triangle,true);

        if(gi.useGroundPlane && ray.y<0)
        {
            const float distance=(scene.geometry->bounds.min.y-origin.y)/ray.y;
    
            if(distance>0 && distance<hit.distance)
            {
                next={};next.point=origin+ray*distance;next.geometric=next.normal={0,1,0};
                next.albedo=gi.groundColor;return true;
            }
        }
    
        if(!hit.found)
        {
            return false;
        }

        next=EvaluateSurface(scene,hit,ray);return true;
    }

    struct Response {Vec3 direct,indirect,sky;float sun=0;};

    Response Bake(const Scene& scene,const Surface& primary,const RenderSettings& settings, Vec3 light,float bias,std::stop_token stop)
    {
        const auto& gi=settings.globalIllumination;Response result;

        result.sun=Sun(scene,primary,light,settings.lighting.shadows,bias);
        result.direct=primary.albedo*result.sun;

        // Per-surface deterministic sampling: no shared RNG or dependence on worker order.
        auto seed=Hash(primary.triangle^Hash(std::bit_cast<std::uint32_t>(primary.point.x))^Hash(std::bit_cast<std::uint32_t>(primary.point.y))^Hash(std::bit_cast<std::uint32_t>(primary.point.z)));

        const float rotation=Random(seed);

        for(int sample=0;sample<gi.samples;++sample)
        {
            if(stop.stop_requested())
            {
                return result;
            }
    
            auto state=Hash(seed+static_cast<unsigned>(sample));
            Surface surface=primary;Vec3 throughput=primary.albedo;

            // bounces counts additional surfaces. At the last surface we still
            // sample sky visibility; a blocked terminal ray contributes nothing.
            for(int depth=0;depth<=gi.bounces;++depth)
            {
                const float u=depth?Random(state):(sample+.5f)/gi.samples;
                const float v=depth?Random(state):std::fmod(RadicalInverse(static_cast<unsigned>(sample))+rotation,1.0f);
                const Vec3 ray=Hemisphere(surface.normal,u,v);
    
                if(Dot(surface.geometric,ray)<=0)
                {
                    break; // Never leak through the geometric surface.
                }

                Surface next;

                if(!NextSurface(scene,surface,ray,gi,bias,next))
                {
                    // Fixed uniform upper-hemisphere sky; the lower hemisphere is black.
                    if(ray.y>0)
                    {
                        result.sky=result.sky+throughput;
                    }
    
                    break;
                }
    
                if(depth==gi.bounces)
                {
                    break;
                }

                throughput=Multiply(throughput,next.albedo);
    
                if(std::max({throughput.x,throughput.y,throughput.z})<1e-6f)
                {
                    break;
                }

                result.indirect=result.indirect+throughput*Sun(scene,next,light,settings.lighting.shadows,bias);
                surface=next;
            }
        }

        result.indirect=result.indirect/static_cast<float>(gi.samples);
        result.sky=result.sky/static_cast<float>(gi.samples);

        return result;
    }
}

void Relight(RenderImage& image,const Lighting& lighting,float giStrength)
{
    const auto pixels=static_cast<std::size_t>(image.width)*image.height;
    if(image.directLight.size()!=pixels || image.indirectLight.size()!=pixels || image.skyLight.size()!=pixels || image.rgba.size()!=pixels*4)
        throw std::invalid_argument("Relighting requires a completed GI bake");
    RenderSettings checked;checked.lighting=lighting;checked.globalIllumination.strength=giStrength;checked.Validate();
    const auto sun=Linear(lighting.directionalColor)*lighting.directionalIntensity;
    const auto sky=Linear(lighting.ambientColor)*lighting.ambientIntensity;
    for(std::size_t i=0;i<pixels;++i)
        StoreColor(image.rgba,i*4,Multiply(image.directLight[i]+image.indirectLight[i]*giStrength,sun)+Multiply(image.skyLight[i],sky));
}

std::vector<std::uint8_t> LightingLayerPreview(const RenderImage& image,std::span<const Vec3> layer)
{
    if(layer.size()*4!=image.rgba.size())throw std::invalid_argument("No GI layer to preview");
    std::vector<std::uint8_t> bytes(image.rgba.size());
    for(std::size_t i=0;i<layer.size();++i){StoreColor(bytes,i*4,layer[i]);bytes[i*4+3]=image.rgba[i*4+3];}
    return bytes;
}

RenderImage RenderGlobalIllumination(const Scene& scene,const RenderSettings& settings,const Frame& frame,
    unsigned quarterTurns,std::stop_token stop,std::atomic<int>* completedRows)
{
    RenderImage result;result.width=settings.width;result.height=settings.height;
    const auto pixels=static_cast<std::size_t>(settings.width)*settings.height;
    result.rgba.resize(pixels*4);result.albedo.resize(pixels*4);result.directional.resize(pixels*4);
    result.directLight.resize(pixels);result.indirectLight.resize(pixels);result.skyLight.resize(pixels);
    const auto basis=ViewBasis(quarterTurns);const Vec3 direction=basis.towardCamera*-1;
    const Vec3 light=Normalize(DirectionalLightDirection);
    const float worldPerPixel=2*frame.halfHeight/settings.height;
    const float bias=std::max(1e-6f,Length(scene.geometry->bounds.max-scene.geometry->bounds.min)*1e-5f);
    const int grid=settings.aaGridSize,total=grid*grid;
    std::atomic<int> nextRow{0};
    const auto work=[&]
    {
        for(int y=nextRow.fetch_add(1);y<settings.height && !stop.stop_requested();y=nextRow.fetch_add(1))
        {
            for(int x=0;x<settings.width;++x)
            {
                const auto pixel=static_cast<std::size_t>(y)*settings.width+x,index=pixel*4;
                Response sum;Vec3 albedo{};int covered=0;
                for(int sy=0;sy<grid;++sy)for(int sx=0;sx<grid;++sx)
                {
                    if(stop.stop_requested())return;
                    const auto origin=frame.center+basis.towardCamera*frame.distance+
                        basis.right*((x+(sx+.5f)/grid-settings.width*.5f)*worldPerPixel)+
                        basis.up*((settings.height*.5f-y-(sy+.5f)/grid)*worldPerPixel);
                    const auto hit=Trace(scene,origin,direction,frame.distance*2);
                    if(!hit.found)continue;
                    const auto surface=EvaluateSurface(scene,hit,direction);
                    const auto response=Bake(scene,surface,settings,light,bias,stop);
                    sum.direct=sum.direct+response.direct;sum.indirect=sum.indirect+response.indirect;
                    sum.sky=sum.sky+response.sky;sum.sun+=response.sun;albedo=albedo+surface.albedo;++covered;
                }
                if(!covered)continue;
                const float divisor=static_cast<float>(covered);
                result.directLight[pixel]=sum.direct/divisor;result.indirectLight[pixel]=sum.indirect/divisor;
                result.skyLight[pixel]=sum.sky/divisor;
                StoreColor(result.albedo,index,albedo/divisor);
                result.directional[index]=result.directional[index+1]=result.directional[index+2]=Byte(sum.sun/divisor);
                result.rgba[index+3]=result.albedo[index+3]=result.directional[index+3]=Byte(divisor/total);
            }
            if(completedRows)completedRows->fetch_add(1,std::memory_order_relaxed);
        }
    };
    const auto count=std::min(16u,std::max(1u,std::thread::hardware_concurrency()));
    std::vector<std::jthread> workers;workers.reserve(count-1);

    for(unsigned i=1;i<count;++i)
    {
        workers.emplace_back(work);
    }

    work();
    
    for(auto& worker:workers)
    {
        worker.join();
    }

    if(stop.stop_requested())
    {
        throw std::runtime_error("Render cancelled");
    }

    Relight(result,settings.lighting,settings.globalIllumination.strength);

    return result;
}
}
