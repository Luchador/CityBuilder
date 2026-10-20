#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "export.hpp"
#include <chrono>
#include <cctype>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

namespace rk=renderkit;
namespace
{
    enum class JobKind { Import, BaseColor, Opacity, Preview, Export };
    struct Result
    {
        JobKind kind{};
        std::uint64_t revision=0;
        int material=0;
        std::optional<rk::Scene> scene;
        std::shared_ptr<const rk::Image> texture;
        std::optional<rk::RenderImage> image;
        std::filesystem::path folder;
        std::string error;
    };
    struct Job
    {
        std::atomic<bool> done{false};
        std::atomic<int> rows{0};
        int totalRows=1;
        JobKind kind{};
        Result result;
        // Declared last: request stop and join before any state is destroyed.
        std::jthread worker;
        void Start(JobKind type,int total,std::function<Result(std::stop_token,std::atomic<int>&)> action)
        {
            kind=type;totalRows=total;
            worker=std::jthread([this,action=std::move(action)](std::stop_token stop)
            {
                try {result=action(stop,rows);}
                catch(const std::exception& e){result.kind=kind;result.error=e.what();}
                catch(...){result.kind=kind;result.error="Unexpected operation failure";}
                done.store(true,std::memory_order_release);
            });
        }
    };
    struct DialogResult { JobKind kind{};int material=0;std::string path,error; };
    struct DialogInbox { std::mutex mutex;std::vector<DialogResult> results;std::atomic<bool> pending{false}; };
    struct DialogRequest {std::shared_ptr<DialogInbox> inbox;JobKind kind;int material;std::string location;};
    void SDLCALL DialogDone(void* userdata,const char* const* files,int)
    {
        std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(userdata));
        DialogResult result;result.kind=request->kind;result.material=request->material;
        if(!files)result.error=std::string("File dialog: ")+SDL_GetError();
        else if(files[0])result.path=files[0];
        {
            std::lock_guard lock(request->inbox->mutex);
            request->inbox->results.push_back(std::move(result));
        }
        request->inbox->pending=false;
    }
    template<std::size_t N> void SetText(char (&buffer)[N],const std::string& value)
    {
        if(value.size()>=N)throw std::runtime_error("Path or name is too long");
        std::copy(value.begin(),value.end(),buffer);buffer[value.size()]=0;
    }
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;

    class Editor
    {
    public:
        ~Editor()
        {
            job.reset();
            preview.reset();
            if(rendererBackend)ImGui_ImplSDLRenderer3_Shutdown();
            if(platformBackend)ImGui_ImplSDL3_Shutdown();
            if(context)ImGui::DestroyContext();
            if(renderer)SDL_DestroyRenderer(renderer);
            if(window)SDL_DestroyWindow(window);
        }
        void Initialize()
        {
            window=SDL_CreateWindow("Cities Unlimited | Render Kit",1440,920,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if(!window)throw std::runtime_error(SDL_GetError());
            SDL_SetWindowMinimumSize(window,1100,720);
            renderer=SDL_CreateRenderer(window,nullptr);
            if(!renderer)throw std::runtime_error(SDL_GetError());
            SDL_SetRenderVSync(renderer,1);
            IMGUI_CHECKVERSION();ImGui::CreateContext();context=true;
            ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::GetIO().IniFilename=nullptr;
            ImGui::StyleColorsDark();
            auto& style=ImGui::GetStyle();
            style.WindowRounding=0;style.FrameRounding=5;style.GrabRounding=5;style.ChildRounding=8;
            style.FramePadding={8,6};style.ItemSpacing={9,9};
            style.Colors[ImGuiCol_WindowBg]={.065f,.08f,.105f,1};
            style.Colors[ImGuiCol_ChildBg]={.085f,.105f,.135f,1};
            style.Colors[ImGuiCol_Header]={.12f,.28f,.31f,1};
            style.Colors[ImGuiCol_HeaderHovered]={.15f,.36f,.40f,1};
            style.Colors[ImGuiCol_HeaderActive]={.17f,.42f,.45f,1};
            style.Colors[ImGuiCol_Button]={.12f,.30f,.34f,1};
            style.Colors[ImGuiCol_ButtonHovered]={.18f,.42f,.46f,1};
            style.Colors[ImGuiCol_ButtonActive]={.15f,.51f,.52f,1};
            style.Colors[ImGuiCol_CheckMark]={.39f,.86f,.76f,1};
            style.FontSizeBase=17;
            if(const char* base=SDL_GetBasePath())
            {
                const std::string font=std::string(base)+"render-kit-data/SourceSans3-Regular.ttf";
                std::size_t size=0;
                if(void* bytes=SDL_LoadFile(font.c_str(),&size))
                {
                    fontData.assign(static_cast<std::uint8_t*>(bytes),static_cast<std::uint8_t*>(bytes)+size);
                    SDL_free(bytes);
                    ImFontConfig config;config.FontDataOwnedByAtlas=false;
                    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontData.data(),static_cast<int>(fontData.size()),17,&config);
                }
            }
            platformBackend=ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);
            rendererBackend=ImGui_ImplSDLRenderer3_Init(renderer);
            if(!platformBackend || !rendererBackend)throw std::runtime_error("Cannot initialize Render Kit UI");
            SetText(outputDirectory,rk::Utf8Path(std::filesystem::current_path()));
        }
        void Load(const std::string& path)
        {
            if(job)return;
            SetText(modelPath,path);
            BeginJob(JobKind::Import,1,[path](auto,auto&)
            {
                Result result;result.kind=JobKind::Import;result.scene=rk::LoadGlb(rk::NativePath(path));return result;
            });
            status="Importing model...";
        }
        void Run(const std::string& smokeCapture={})
        {
            bool running=true;int smokeFrames=0;
            const Uint64 smokeStart=SDL_GetTicks();
            while(running)
            {
                SDL_Event event;
                while(SDL_PollEvent(&event))
                {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID==SDL_GetWindowID(window)))running=false;
                    if(event.type==SDL_EVENT_DROP_FILE && !job && !inbox->pending)
                    {
                        try
                        {
                            auto path=rk::NativePath(event.drop.data);
                            auto extension=path.extension().string();
                            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                            if(extension==".glb")Load(event.drop.data);
                            else if(extension==".png" && scene)LoadTexture(event.drop.data,JobKind::BaseColor,selectedMaterial);
                        }
                        catch(const std::exception& e){status=e.what();}
                    }
                }
                PollDialogs();PollJob();
                if(dirty && scene && !job && !inbox->pending && SDL_GetTicks()-lastChange>180)StartPreview();
                if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED){SDL_Delay(16);continue;}
                ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
                Draw();
                ImGui::Render();
                SDL_SetRenderScale(renderer,ImGui::GetIO().DisplayFramebufferScale.x,ImGui::GetIO().DisplayFramebufferScale.y);
                SDL_SetRenderDrawColor(renderer,17,21,27,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
                if(!smokeCapture.empty() && preview && !job && !dirty && ++smokeFrames>=3)
                {
                    auto* pixels=SDL_RenderReadPixels(renderer,nullptr);
                    if(!pixels)throw std::runtime_error(SDL_GetError());
                    const bool saved=SDL_SavePNG(pixels,smokeCapture.c_str());SDL_DestroySurface(pixels);
                    if(!saved)throw std::runtime_error(SDL_GetError());
                    running=false;
                }
                SDL_RenderPresent(renderer);
                if(!smokeCapture.empty() && SDL_GetTicks()-smokeStart>30'000)throw std::runtime_error("UI smoke test timed out: "+status);
                SDL_Delay(8);
            }
        }
    private:
        void BeginJob(JobKind type,int total,std::function<Result(std::stop_token,std::atomic<int>&)> action)
        {
            auto pending=std::make_unique<Job>();
            pending->Start(type,total,std::move(action));
            job=std::move(pending);
        }
        void Changed()
        {
            ++revision;dirty=true;lastChange=SDL_GetTicks();
            if(job && job->kind==JobKind::Preview)job->worker.request_stop();
        }
        void LoadTexture(const std::string& path,JobKind kind,int material)
        {
            if(job || !scene)return;
            BeginJob(kind,1,[path,kind,material](auto,auto&)
            {
                Result result;result.kind=kind;result.material=material;result.texture=rk::LoadPng(rk::NativePath(path));return result;
            });
            status="Loading PNG...";
        }
        void StartPreview()
        {
            try
            {
                settings.Validate();
                const auto outputSettings=settings;
                rk::RenderSettings previewSettings=settings;
                const float scale=std::max(std::min(1.0f,640.0f/std::max(settings.width,settings.height)),
                                           std::min(1.0f,16.0f/std::min(settings.width,settings.height)));
                previewSettings.width=std::max(16,static_cast<int>(std::lround(settings.width*scale)));
                previewSettings.height=std::max(16,static_cast<int>(std::lround(settings.height*scale)));
                // The same world framing as export, even in manual pixels-per-unit mode.
                const auto snapshot=*scene;const auto requestedRevision=revision;const auto angle=view;
                BeginJob(JobKind::Preview,previewSettings.height,[snapshot,previewSettings,outputSettings,requestedRevision,angle](auto stop,auto& rows)
                {
                    Result result;result.kind=JobKind::Preview;result.revision=requestedRevision;
                    const auto frame=rk::FitFrame(*snapshot.geometry,outputSettings);
                    result.image=rk::Render(snapshot,previewSettings,frame,static_cast<unsigned>(angle),stop,&rows);return result;
                });
                dirty=false;
            }
            catch(const std::exception& e){status=e.what();dirty=false;}
        }
        void PollJob()
        {
            if(!job || !job->done.load(std::memory_order_acquire))return;
            const bool cancelled=job->worker.get_stop_token().stop_requested();
            job->worker.join();Result result=std::move(job->result);job.reset();
            if(cancelled)
            {
                if(result.kind!=JobKind::Preview)status="Operation cancelled.";
                return;
            }
            if(!result.error.empty()){status=result.error;return;}
            try
            {
                switch(result.kind)
                {
                    case JobKind::Import:
                        scene=std::move(result.scene);selectedMaterial=0;preview.reset();previewImage.reset();
                        status="Model loaded. Choose a material to edit its textures and cut-out settings.";Changed();break;
                    case JobKind::BaseColor:
                    case JobKind::Opacity:
                        if(scene && result.material>=0 && result.material<static_cast<int>(scene->materials.size()))
                        {
                            auto& material=scene->materials[static_cast<std::size_t>(result.material)];
                            if(result.kind==JobKind::BaseColor)material.baseColor=std::move(result.texture);
                            else material.opacity=std::move(result.texture);
                            status="PNG assigned.";Changed();
                        }
                        break;
                    case JobKind::Preview:
                        if(result.revision==revision){previewImage=std::move(result.image);UploadPreview();}
                        break;
                    case JobKind::Export:
                        status="Exported to "+rk::Utf8Path(result.folder);lastExport=result.folder;break;
                }
            }
            catch(const std::exception& e){status=e.what();}
        }
        void UploadPreview()
        {
            if(!previewImage)return;
            const auto& image=*previewImage;
            std::vector<std::uint8_t> alpha;
            const auto* bytes=&image.rgba;
            if(previewChannel==1)
            {
                alpha.resize(image.rgba.size());
                for(std::size_t i=0;i<alpha.size();i+=4){alpha[i]=alpha[i+1]=alpha[i+2]=image.rgba[i+3];alpha[i+3]=255;}
                bytes=&alpha;
            }
            else if(previewChannel==2)bytes=&image.albedo;
            else if(previewChannel==3)bytes=&image.directional;
            Texture texture(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,image.width,image.height),SDL_DestroyTexture);
            if(!texture || !SDL_UpdateTexture(texture.get(),nullptr,bytes->data(),image.width*4))throw std::runtime_error(SDL_GetError());
            SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_NEAREST);
            preview=std::move(texture);
        }
        void Browse(JobKind kind,int material=0)
        {
            if(job || inbox->pending)return;
            inbox->pending=true;
            auto* request=new DialogRequest{inbox,kind,material,kind==JobKind::Export?outputDirectory:modelPath};
            static const SDL_DialogFileFilter glb[]={ {"glTF binary model","glb"} };
            static const SDL_DialogFileFilter png[]={ {"PNG image","png"} };
            if(kind==JobKind::Export)SDL_ShowOpenFolderDialog(DialogDone,request,window,request->location.c_str(),false);
            else SDL_ShowOpenFileDialog(DialogDone,request,window,kind==JobKind::Import?glb:png,1,request->location.empty()?nullptr:request->location.c_str(),false);
        }
        void PollDialogs()
        {
            std::vector<DialogResult> results;
            {std::lock_guard lock(inbox->mutex);results.swap(inbox->results);}
            for(auto& result:results)
            {
                try
                {
                    if(!result.error.empty()){status=result.error;continue;}
                    if(result.path.empty())continue;
                    if(result.kind==JobKind::Import)Load(result.path);
                    else if(result.kind==JobKind::Export)SetText(outputDirectory,result.path);
                    else LoadTexture(result.path,result.kind,result.material);
                }
                catch(const std::exception& e){status=e.what();}
            }
        }
        void StartExport()
        {
            try
            {
                settings.Validate();
                const auto snapshot=*scene;const auto options=settings;const auto directory=rk::NativePath(outputDirectory);
                const std::string name=prefix;const bool layers=exportLayers;
                BeginJob(JobKind::Export,settings.height*4,[snapshot,options,directory,name,layers](auto stop,auto& rows)
                {
                    Result result;result.kind=JobKind::Export;
                    result.folder=rk::ExportViews(snapshot,options,directory,name,layers,stop,&rows);return result;
                });
                status="Rendering four views...";
            }
            catch(const std::exception& e){status=e.what();}
        }
        void MaterialPanel()
        {
            ImGui::TextColored({.39f,.86f,.76f,1},"MODEL & MATERIALS");
            ImGui::BeginDisabled(job || inbox->pending);
            if(ImGui::Button("Import GLB...",{-1,0}))Browse(JobKind::Import);
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##modelPath","Model path (.glb)",modelPath,sizeof(modelPath));
            if(ImGui::Button("Load path",{-1,0}) && modelPath[0])Load(modelPath);
            ImGui::EndDisabled();
            if(!scene){ImGui::TextWrapped("Import or drop a GLB model here. PNG files can be assigned to each material.");return;}
            ImGui::Text("%zu triangles | %zu materials",scene->geometry->triangles.size(),scene->materials.size());
            ImGui::Separator();
            ImGui::BeginChild("Material list",{0,140},ImGuiChildFlags_Borders);
            for(std::size_t i=0;i<scene->materials.size();++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if(ImGui::Selectable(scene->materials[i].name.c_str(),selectedMaterial==static_cast<int>(i)))selectedMaterial=static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndChild();
            auto& material=scene->materials[static_cast<std::size_t>(selectedMaterial)];
            ImGui::TextWrapped("%s",material.name.c_str());
            int mode=static_cast<int>(material.alphaMode);
            ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##alphaMode",&mode,"Opaque\0Alpha cut-out\0")){material.alphaMode=static_cast<rk::AlphaMode>(mode);Changed();}
            ImGui::BeginDisabled(material.alphaMode==rk::AlphaMode::Opaque);
            if(ImGui::SliderFloat("Cut-off",&material.cutoff,0,1,"%.3f"))Changed();
            ImGui::EndDisabled();
            if(ImGui::Checkbox("Double-sided",&material.doubleSided))Changed();
            float color[3]={rk::LinearToSrgb(material.baseFactor.r),rk::LinearToSrgb(material.baseFactor.g),rk::LinearToSrgb(material.baseFactor.b)};
            if(ImGui::ColorEdit3("Tint",color))
            {material.baseFactor.r=rk::SrgbToLinear(color[0]);material.baseFactor.g=rk::SrgbToLinear(color[1]);material.baseFactor.b=rk::SrgbToLinear(color[2]);Changed();}
            if(ImGui::SliderFloat("Factor alpha",&material.baseFactor.a,0,1))Changed();
            ImGui::SeparatorText("Base color PNG");
            ImGui::TextWrapped("%s",material.baseColor?material.baseColor->name.c_str():"No texture (uses tint)");
            ImGui::BeginDisabled(job || inbox->pending);
            if(ImGui::Button("Assign base color..."))Browse(JobKind::BaseColor,selectedMaterial);
            ImGui::SameLine();if(ImGui::Button("Clear##base")){material.baseColor.reset();Changed();}
            ImGui::EndDisabled();
            ImGui::SeparatorText("Opacity PNG");
            ImGui::TextWrapped("%s",material.opacity?material.opacity->name.c_str():"No separate opacity texture");
            ImGui::BeginDisabled(job || inbox->pending);
            if(ImGui::Button("Assign opacity..."))Browse(JobKind::Opacity,selectedMaterial);
            ImGui::SameLine();if(ImGui::Button("Clear##opacity")){material.opacity.reset();Changed();}
            ImGui::EndDisabled();
            int channel=static_cast<int>(material.opacityChannel);
            if(ImGui::Combo("Channel",&channel,"Grayscale / luminance\0Alpha\0Red\0")){material.opacityChannel=static_cast<rk::OpacityChannel>(channel);Changed();}
            ImGui::TextWrapped("Cut-out multiplies base-color alpha, factor alpha, vertex alpha and the opacity map. Rejected pixels reveal surfaces behind them.");
            if(ImGui::TreeNode("UV settings"))
            {
                if(ImGui::Combo("UV set",&material.texcoord,"TEXCOORD_0\0TEXCOORD_1\0"))Changed();
                float scale[2]={material.uvScale.x,material.uvScale.y},offset[2]={material.uvOffset.x,material.uvOffset.y};
                if(ImGui::DragFloat2("Scale",scale,.01f,-100,100)){material.uvScale={scale[0],scale[1]};Changed();}
                if(ImGui::DragFloat2("Offset",offset,.01f,-100,100)){material.uvOffset={offset[0],offset[1]};Changed();}
                if(ImGui::Button("Reset UV transform")){material.uvScale={1,1};material.uvOffset={};material.uvRotation=0;Changed();}
                ImGui::TreePop();
            }
            if(!scene->warnings.empty() && ImGui::TreeNode("Import notes"))
            {
                for(const auto& warning:scene->warnings)ImGui::TextWrapped("%s",warning.c_str());
                ImGui::TreePop();
            }
        }
        void OutputPanel()
        {
            ImGui::TextColored({.39f,.86f,.76f,1},"LIGHTING");
            ImGui::TextWrapped("Fixed world-space light\nX -0.75   Y 0.50   Z 0.25");
            auto& light=settings.lighting;
            float ambient[]={light.ambientColor.x,light.ambientColor.y,light.ambientColor.z};
            float sun[]={light.directionalColor.x,light.directionalColor.y,light.directionalColor.z};
            if(ImGui::ColorEdit3("Ambient",ambient)){light.ambientColor={ambient[0],ambient[1],ambient[2]};Changed();}
            if(ImGui::SliderFloat("Ambient strength",&light.ambientIntensity,0,3,"%.2f"))Changed();
            if(ImGui::ColorEdit3("Directional",sun)){light.directionalColor={sun[0],sun[1],sun[2]};Changed();}
            if(ImGui::SliderFloat("Sun strength",&light.directionalIntensity,0,3,"%.2f"))Changed();
            if(ImGui::Checkbox("Self shadows",&light.shadows))Changed();
            if(ImGui::Button("Reset lighting")){light=rk::Lighting{};Changed();}
            ImGui::Separator();ImGui::TextColored({.39f,.86f,.76f,1},"OUTPUT");
            int resolution[]={settings.width,settings.height};
            if(ImGui::InputInt2("Size (px)",resolution))
            {settings.width=resolution[0];settings.height=resolution[1];Changed();}
            if(ImGui::IsItemDeactivatedAfterEdit())
            {settings.width=std::clamp(settings.width,16,4096);settings.height=std::clamp(settings.height,16,4096);Changed();}
            for(int size:{256,512,1024})
            {
                if(size!=256)ImGui::SameLine();
                if(ImGui::Button(std::to_string(size).c_str())){settings.width=settings.height=size;Changed();}
            }
            if(ImGui::Checkbox("Fit all four views",&settings.autoFit))Changed();
            if(settings.autoFit)
            {if(ImGui::SliderFloat("Padding",&settings.padding,0,.3f,"%.2f"))Changed();}
            else
            {if(ImGui::DragFloat("Pixels / unit",&settings.pixelsPerUnit,1,1,4096,"%.1f",ImGuiSliderFlags_AlwaysClamp))Changed();}
            ImGui::TextWrapped("One common scale and center for all views. 1 model unit = 1 game tile.");
            ImGui::Checkbox("Export day/night layers",&exportLayers);
            if(exportLayers)ImGui::TextWrapped("Adds unlit color and a directional-light mask for each view.");
            ImGui::InputText("Prefix",prefix,sizeof(prefix));
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##outputDirectory","Export folder",outputDirectory,sizeof(outputDirectory));
            ImGui::BeginDisabled(job || inbox->pending);
            if(ImGui::Button("Choose folder...",{-1,0}))Browse(JobKind::Export);
            ImGui::BeginDisabled(!scene);
            if(ImGui::Button("Export four views",{-1,42}))StartExport();
            ImGui::EndDisabled();ImGui::EndDisabled();
            ImGui::TextWrapped("RGBA PNG. Alpha is white on model hits and black on misses. Each export gets a new numbered folder.");
            if(!lastExport.empty())
            {
                ImGui::SeparatorText("Last export");
                ImGui::TextWrapped("%s",rk::Utf8Path(lastExport).c_str());
            }
        }
        void PreviewPanel()
        {
            ImGui::TextColored({.39f,.86f,.76f,1},"ORTHOGRAPHIC PREVIEW");
            for(int angle=0;angle<4;++angle)
            {
                if(angle)ImGui::SameLine();
                const auto label="View "+std::to_string(angle);
                const bool selected=view==angle;
                if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.18f,.46f,.44f,1));
                if(ImGui::Button(label.c_str())){view=angle;Changed();}
                if(selected)ImGui::PopStyleColor();
            }
            ImGui::Text("Pitch 33 deg | Yaw %d deg | clockwise quarter-turn %d",45+view*90,view);
            ImGui::SetNextItemWidth(200);
            if(ImGui::Combo("Display",&previewChannel,"Lit sprite\0Alpha mask\0Unlit color\0Sun mask\0"))UploadPreview();
            const ImVec2 available=ImGui::GetContentRegionAvail();
            const ImVec2 top=ImGui::GetCursorScreenPos();
            const float width=std::max(1.0f,available.x),height=std::max(1.0f,available.y);
            auto* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(top,{top.x+width,top.y+height},IM_COL32(14,18,24,255),6);
            if(preview && previewImage)
            {
                const float ratio=std::min((width-24)/previewImage->width,(height-24)/previewImage->height);
                const ImVec2 size{previewImage->width*ratio,previewImage->height*ratio};
                const ImVec2 start{top.x+(width-size.x)*.5f,top.y+(height-size.y)*.5f},end{start.x+size.x,start.y+size.y};
                draw->PushClipRect(start,end,true);
                for(int y=0;y<static_cast<int>(size.y);y+=16)for(int x=0;x<static_cast<int>(size.x);x+=16)
                    draw->AddRectFilled({start.x+x,start.y+y},{start.x+x+16,start.y+y+16},((x/16+y/16)%2)?IM_COL32(49,57,65,255):IM_COL32(39,46,54,255));
                draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(preview.get())),start,end);
                draw->PopClipRect();draw->AddRect(start,end,IM_COL32(83,105,118,255));
            }
            else draw->AddText({top.x+24,top.y+height*.45f},IM_COL32(165,182,191,255),"Import a GLB to preview your sprite.");
            ImGui::Dummy({width,height});
        }
        void Draw()
        {
            const auto* viewport=ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::Begin("Render Kit",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored({.75f,.94f,.90f,1},"RENDER KIT");ImGui::SameLine();ImGui::TextDisabled("/  CITIES UNLIMITED");
            ImGui::Separator();
            const float height=std::max(200.0f,ImGui::GetContentRegionAvail().y-88);
            const float panelWidth=std::clamp(ImGui::GetContentRegionAvail().x*.25f,285.0f,350.0f);
            ImGui::BeginChild("Materials",{panelWidth,height},ImGuiChildFlags_Borders);
            ImGui::PushItemWidth(-125);
            ImGui::BeginDisabled(job && job->kind==JobKind::Export);MaterialPanel();ImGui::EndDisabled();ImGui::PopItemWidth();ImGui::EndChild();
            ImGui::SameLine();
            const float middle=std::max(220.0f,ImGui::GetContentRegionAvail().x-panelWidth-ImGui::GetStyle().ItemSpacing.x);
            ImGui::BeginChild("Preview",{middle,height},ImGuiChildFlags_Borders);PreviewPanel();ImGui::EndChild();
            ImGui::SameLine();ImGui::BeginChild("Output",{0,height},ImGuiChildFlags_Borders);
            ImGui::PushItemWidth(-125);
            ImGui::BeginDisabled(job && job->kind==JobKind::Export);OutputPanel();ImGui::EndDisabled();ImGui::PopItemWidth();ImGui::EndChild();
            if(job)
            {
                const float fraction=std::clamp(static_cast<float>(job->rows.load())/job->totalRows,0.0f,1.0f);
                ImGui::ProgressBar(fraction,{ImGui::GetContentRegionAvail().x-105,20});ImGui::SameLine();
                if(ImGui::Button("Cancel"))job->worker.request_stop();
            }
            ImGui::TextWrapped("%s",status.c_str());
            ImGui::End();
        }
        SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
        bool context=false,platformBackend=false,rendererBackend=false;
        std::vector<std::uint8_t> fontData;
        std::optional<rk::Scene> scene;
        rk::RenderSettings settings;
        int selectedMaterial=0,view=0,previewChannel=0;
        bool exportLayers=true,dirty=false;
        std::uint64_t revision=0;Uint64 lastChange=0;
        Texture preview{nullptr,SDL_DestroyTexture};
        std::optional<rk::RenderImage> previewImage;
        std::unique_ptr<Job> job;
        std::shared_ptr<DialogInbox> inbox=std::make_shared<DialogInbox>();
        char modelPath[4096]{},outputDirectory[4096]{},prefix[96]="sprite";
        std::string status="Import a GLB model to begin. PNG drop assigns base color to the selected material.";
        std::filesystem::path lastExport;
    };
}
int main(int argc,char** argv)
{
    try
    {
        // The same renderer can be used without a window for automated exports.
        if(argc>=2 && std::string_view(argv[1])=="--render")
        {
            if(argc<4 || argc>6)throw std::invalid_argument("Usage: RenderKit --render model.glb output_folder [width height]");
            if(!SDL_Init(0))throw std::runtime_error(SDL_GetError());
            rk::RenderSettings settings;
            if(argc>=5)settings.width=settings.height=std::stoi(argv[4]);
            if(argc==6)settings.height=std::stoi(argv[5]);
            const auto scene=rk::LoadGlb(rk::NativePath(argv[2]));
            const auto folder=rk::ExportViews(scene,settings,rk::NativePath(argv[3]),"sprite",true);
            std::cout<<"Exported to "<<rk::Utf8Path(folder)<<'\n';SDL_Quit();return 0;
        }
        if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
        {
            Editor editor;editor.Initialize();
            if(argc==4 && std::string_view(argv[1])=="--smoke-test")
            {editor.Load(argv[2]);editor.Run(argv[3]);}
            else {if(argc==2)editor.Load(argv[1]);editor.Run();}
        }
        SDL_Quit();return 0;
    }
    catch(const std::exception& e)
    {
        SDL_Log("Render Kit: %s",e.what());std::cerr<<e.what()<<'\n';
        if(SDL_WasInit(SDL_INIT_VIDEO))SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Render Kit",e.what(),nullptr);
        SDL_Quit();return 1;
    }
}
