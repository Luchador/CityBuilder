#include "ui_manager.hpp"
#include "gpu_utils.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

// Real manager/components, with device/font doubles for portable regression checks.
struct SDL_Window { int width=1280,height=720,scale=1; SDL_WindowFlags flags=SDL_WINDOW_INPUT_FOCUS|SDL_WINDOW_MOUSE_FOCUS; };
struct SDL_GPUDevice {}; struct SDL_GPUShader {}; struct SDL_GPUGraphicsPipeline {};
struct SDL_GPUBuffer { std::vector<Uint8> bytes; };
struct SDL_GPUTransferBuffer { std::vector<Uint8> bytes; };
struct SDL_GPUCommandBuffer {}; struct SDL_GPUCopyPass {}; struct SDL_GPURenderPass {}; struct SDL_GPUTexture {};
namespace {
int liveShaders=0,livePipelines=0,liveBuffers=0,liveTransfers=0,shaderAttempts=0,failShaderAt=0;
int geometryUploads=0,mapUploads=0,outlineUploads=0,textPrepares=0;
float textScale=0;
Uint64 clockMilliseconds=0;
std::vector<std::string> draws;
std::unordered_map<const Texture*,std::string> imageNames;
std::unordered_map<const TextLabel*,std::string> labelValues;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool NoGpuObjects(){return liveShaders==0&&livePipelines==0&&liveBuffers==0&&liveTransfers==0;}
}
const char* SDLCALL SDL_GetError(){return "test error";}
Uint64 SDLCALL SDL_GetTicks(){return clockMilliseconds;}
SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window* w){return w->flags;}
bool SDLCALL SDL_GetWindowSize(SDL_Window* w,int* x,int* y){*x=w->width;*y=w->height;return true;}
bool SDLCALL SDL_GetWindowSizeInPixels(SDL_Window* w,int* x,int* y){*x=w->width*w->scale;*y=w->height*w->scale;return true;}
bool SDLCALL SDL_CaptureMouse(bool){return true;}
SDL_MouseButtonFlags SDLCALL SDL_GetMouseState(float* x,float* y){*x=20;*y=20;return 0;}
SDL_GPUTextureFormat SDLCALL SDL_GetGPUSwapchainTextureFormat(SDL_GPUDevice*,SDL_Window*){return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;}
SDL_GPUShader* CompileShader(SDL_GPUDevice*,const char*,SDL_GPUShaderStage,Uint32,const char*,Uint32){
 if(++shaderAttempts==failShaderAt)throw std::runtime_error("injected shader failure");
 ++liveShaders;return new SDL_GPUShader;
}
void SDLCALL SDL_ReleaseGPUShader(SDL_GPUDevice*,SDL_GPUShader* p){--liveShaders;delete p;}
SDL_GPUGraphicsPipeline* SDLCALL SDL_CreateGPUGraphicsPipeline(SDL_GPUDevice*,const SDL_GPUGraphicsPipelineCreateInfo* i){
 Check(!i->target_info.has_depth_stencil_target&&!i->depth_stencil_state.enable_depth_test,"UI must not use terrain depth");
 ++livePipelines;return new SDL_GPUGraphicsPipeline;
}
void SDLCALL SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice*,SDL_GPUGraphicsPipeline* p){--livePipelines;delete p;}
SDL_GPUBuffer* SDLCALL SDL_CreateGPUBuffer(SDL_GPUDevice*,const SDL_GPUBufferCreateInfo* i){++liveBuffers;return new SDL_GPUBuffer{std::vector<Uint8>(i->size)};}
void SDLCALL SDL_ReleaseGPUBuffer(SDL_GPUDevice*,SDL_GPUBuffer* p){--liveBuffers;delete p;}
SDL_GPUBuffer* UploadBuffer(SDL_GPUDevice* d,const void* data,Uint32 size,SDL_GPUBufferUsageFlags usage){
 SDL_GPUBufferCreateInfo i{};i.usage=usage;i.size=size;auto* b=SDL_CreateGPUBuffer(d,&i);
 std::memcpy(b->bytes.data(),data,size);++geometryUploads;return b;
}
SDL_GPUTransferBuffer* SDLCALL SDL_CreateGPUTransferBuffer(SDL_GPUDevice*,const SDL_GPUTransferBufferCreateInfo* i){++liveTransfers;return new SDL_GPUTransferBuffer{std::vector<Uint8>(i->size)};}
void SDLCALL SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice*,SDL_GPUTransferBuffer* p){--liveTransfers;delete p;}
void* SDLCALL SDL_MapGPUTransferBuffer(SDL_GPUDevice*,SDL_GPUTransferBuffer* p,bool){return p->bytes.data();}
void SDLCALL SDL_UnmapGPUTransferBuffer(SDL_GPUDevice*,SDL_GPUTransferBuffer*){}
SDL_GPUCopyPass* SDLCALL SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer*){static SDL_GPUCopyPass p;return &p;}
void SDLCALL SDL_EndGPUCopyPass(SDL_GPUCopyPass*){}
void SDLCALL SDL_UploadToGPUBuffer(SDL_GPUCopyPass*,const SDL_GPUTransferBufferLocation* a,const SDL_GPUBufferRegion* b,bool){
 std::memcpy(b->buffer->bytes.data()+b->offset,a->transfer_buffer->bytes.data()+a->offset,b->size);++outlineUploads;
}
SDL_GPURenderPass* SDLCALL SDL_BeginGPURenderPass(SDL_GPUCommandBuffer*,const SDL_GPUColorTargetInfo* t,Uint32 n,const SDL_GPUDepthStencilTargetInfo* d){
 Check(n==1&&t->load_op==SDL_GPU_LOADOP_LOAD&&!d,"UI must preserve the world image");static SDL_GPURenderPass p;return &p;
}
void SDLCALL SDL_EndGPURenderPass(SDL_GPURenderPass*){}
void SDLCALL SDL_SetGPUViewport(SDL_GPURenderPass*,const SDL_GPUViewport*){}
void SDLCALL SDL_SetGPUScissor(SDL_GPURenderPass*,const SDL_Rect*){}
void SDLCALL SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass*,SDL_GPUGraphicsPipeline*){}
void SDLCALL SDL_BindGPUVertexBuffers(SDL_GPURenderPass*,Uint32,const SDL_GPUBufferBinding*,Uint32){}
void SDLCALL SDL_PushGPUVertexUniformData(SDL_GPUCommandBuffer*,Uint32,const void*,Uint32){}
void SDLCALL SDL_DrawGPUPrimitives(SDL_GPURenderPass*,Uint32,Uint32,Uint32,Uint32){draws.push_back("Solid");}
Texture::~Texture(){imageNames.erase(this);}
void TextureManager::Release() noexcept {textures.clear();}
const Texture& TextureManager::Load(std::string_view name){
 auto& p=textures[std::string(name)];if(!p){p.reset(new Texture(nullptr));p->width=p->height=64;imageNames[p.get()]=name;}return *p;
}
std::unique_ptr<Texture> TextureManager::CreateRgba(Uint32 w,Uint32 h,std::span<const Uint8>){
 std::unique_ptr<Texture> p(new Texture(nullptr));p->width=w;p->height=h;imageNames[p.get()]="Map";++mapUploads;return p;
}
void UiImageRenderer::Initialize(SDL_GPUDevice* gpu,SDL_GPUShader*,SDL_GPUShader*,SDL_GPUTextureFormat){device=gpu;}
void UiImageRenderer::Release(){device=nullptr;}
void UiImageRenderer::Draw(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,const Texture& t,UiRect,float,float,UiImageTint,UiImageTransform)const{draws.push_back(imageNames.at(&t));}
TextLabel::~TextLabel(){labelValues.erase(this);}
void TextLabel::SetText(std::string_view text){value=text;labelValues[this]=value;}
void TextLabel::SetStyle(const TextStyle& next){style=next;}
void TextLabel::SetBounds(UiRect next){bounds=next;}
void TextRenderer::Initialize(SDL_GPUDevice* gpu,SDL_GPUShader*,SDL_GPUShader*,SDL_GPUTextureFormat){device=gpu;}
TextLabel& TextRenderer::CreateLabel(std::string_view text,const TextStyle& style){
 auto p=std::unique_ptr<TextLabel>(new TextLabel(dirty));p->SetText(text);p->SetStyle(style);labels.push_back(std::move(p));return *labels.back();
}
void TextRenderer::Release() noexcept {labels.clear();device=nullptr;}
void TextRenderer::Prepare(float scale){textScale=scale;++textPrepares;}
void TextRenderer::Draw(SDL_GPUCommandBuffer*,SDL_GPURenderPass*,float,float,Uint32,Uint32)const{draws.push_back("Text");}

namespace
{
    struct Fixture
    {
        SDL_GPUDevice device;
        SDL_Window window;
        TextureManager textures;
        City city;
        Camera camera{MapExtent};
        UiManager ui;
        UiButtons referenceButtons;
        UiMinimap referenceMinimap;
        int cancelCount = 0, moveCount = 0;
        float zoomTotal = 0, mapX = -1, mapZ = -1;
        std::vector<bool> rotations;
        std::vector<UiButtonId> clicks;
        std::vector<int> speedRequests;
        int resumeRequests = 0;

        Fixture()
        {
            UiActions actions;
            actions.zoomCamera = [this](float steps) { zoomTotal += steps; };
            actions.rotateCamera = [this](bool clockwise) { rotations.push_back(clockwise); };
            actions.cancelWorldInteraction = [this]
            {
                ++cancelCount;
                camera.EndMousePan();
            };
            actions.moveCameraToMapPoint = [this](float x, float z)
            {
                ++moveCount;
                mapX = x;
                mapZ = z;
            };
            actions.buttonClicked = [this](UiButtonId id) { clicks.push_back(id); };
            actions.setSimulationSpeed = [this](int speed) { speedRequests.push_back(speed); };
            actions.resumeSimulation = [this] { ++resumeRequests; };
            ui.Initialize(&device, &window, textures, city, std::move(actions));
            CreateUiButtons(referenceButtons, textures);
        }

        std::array<float, 2> Center(UiButtonId id)
        {
            LayoutUiButtons(referenceButtons, referenceMinimap, ui.GetLayout());
            const auto bounds = referenceButtons.Find(id)->GetBounds();
            return {bounds.x + bounds.w / 2, bounds.y + bounds.h / 2};
        }

        bool Mouse(Uint32 type, std::array<float, 2> point, Uint8 button = SDL_BUTTON_LEFT)
        {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_EVENT_MOUSE_MOTION)
            {
                event.motion.x = point[0];
                event.motion.y = point[1];
            }
            else if (type == SDL_EVENT_MOUSE_WHEEL)
            {
                event.wheel.mouse_x = point[0];
                event.wheel.mouse_y = point[1];
            }
            else
            {
                event.button.x = point[0];
                event.button.y = point[1];
                event.button.button = button;
            }
            return ui.HandleEvent(event);
        }

        bool Key(SDL_Keycode key, bool repeat = false)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.repeat = repeat;
            return ui.HandleEvent(event);
        }

        void Click(UiButtonId id)
        {
            const auto point = Center(id);
            Check(Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, point), "missed press");
            Check(Mouse(SDL_EVENT_MOUSE_BUTTON_UP, point), "missed release");
        }
    };

    void InputRouting()
    {
        Fixture f;
        f.Click(UiButtonIds::ZoomInButton);
        f.Click(UiButtonIds::ZoomOutButton);
        Check(f.zoomTotal == 0, "zoom directions changed");
        f.Click(UiButtonIds::RotateClockwiseButton);
        f.Click(UiButtonIds::RotateCounterClockwiseButton);
        Check(f.rotations == std::vector<bool>{true, false}, "rotation directions changed");
        f.Click(UiButtonIds::TickerSpeedButton3);
        Check(f.speedRequests == std::vector<int>{3}, "speed callback lost");
        f.Click(UiButtonIds::TickerPauseButton);
        f.Click(UiButtonIds::TickerPlayButton);
        Check(f.speedRequests == std::vector<int>{3, 0} && f.resumeRequests == 1,
              "pause/play callbacks lost");

        f.referenceMinimap.SetLayout(f.ui.GetLayout());
        const auto point = f.referenceMinimap.MapToScreen(0.25f, 0.75f);
        Check(f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, {point.x, point.y}) && f.moveCount == 1 &&
              std::abs(f.mapX - 0.25f) < 0.001f && std::abs(f.mapZ - 0.75f) < 0.001f,
              "minimap picking changed");
        Check(f.Mouse(SDL_EVENT_MOUSE_WHEEL, {20, 710}) &&
              f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, {20, 710}, SDL_BUTTON_RIGHT),
              "dock input leaked to world");
        Check(!f.Mouse(SDL_EVENT_MOUSE_BUTTON_UP, {20, 710}, SDL_BUTTON_RIGHT),
              "pan release was swallowed");
        Check(!f.Mouse(SDL_EVENT_MOUSE_WHEEL, {20, 20}), "world wheel was swallowed");

        f.Click(UiButtonIds::ZonesButton);
        Check(f.Key(SDLK_ESCAPE) && !f.Key(SDLK_ESCAPE), "Escape dispatch changed");
        f.Click(UiButtonIds::ZonesButton);
        f.Click(UiButtonIds::ResidentialButton);
        Check(f.clicks.back() == UiButtonIds::ResidentialButton, "flyout selection lost");
        f.ui.CancelInteraction();

        const auto zoom = f.Center(UiButtonIds::ZoomInButton);
        f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, zoom);
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        Check(!f.ui.HandleEvent(event), "focus loss must reach world");
        f.Mouse(SDL_EVENT_MOUSE_BUTTON_UP, zoom);
        Check(f.zoomTotal == 0, "focus loss left delayed click");

        f.Click(UiButtonIds::MinimizeUIButton);
        Check(f.ui.IsMinimized() && f.ui.IsWorldPoint(20, 710), "hidden UI still blocks world");
        Check(!f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, zoom), "hidden button still blocks world");
        Check(f.Key(SDLK_TAB) && !f.ui.IsMinimized(), "Tab did not restore UI");
        f.Key(SDLK_TAB, true);
        Check(!f.ui.IsMinimized(), "Tab repeated");

        f.window.width = 1024;
        f.window.height = 768;
        event.type = SDL_EVENT_WINDOW_RESIZED;
        f.ui.HandleEvent(event);
        f.Click(UiButtonIds::ZoomInButton);
        Check(f.zoomTotal == 1, "resize left stale click bounds");
        auto settings = f.ui.GetSettings();
        settings.minimapSize = 400;
        f.ui.SetSettings(settings);
        Check(f.ui.GetLayout().minimapPanel.w == 400, "settings did not refresh layout");
    }

    void RenderingAndLifetime()
    {
        Fixture f;
        f.window.scale = 2;
        f.city.name = "Manager City";
        f.ui.Update(f.city, 0);
        Check(std::any_of(labelValues.begin(), labelValues.end(),
                         [](const auto& pair) { return pair.second == "Manager City"; }),
              "city text did not update");

        TerrainMesh terrain;
        terrain.vertices.resize(TerrainVertexCount);
        for (std::uint32_t z = 0; z <= MapSize; ++z)
        {
            for (std::uint32_t x = 0; x <= MapSize; ++x)
            {
                auto& vertex = terrain.vertices[z * TerrainStride + x];
                vertex.x = static_cast<float>(x);
                vertex.z = static_cast<float>(z);
                vertex.y = 1;
                vertex.g = 0.5f;
            }
        }

        f.ui.RebuildMinimap(terrain);
        const int maps = mapUploads, uploads = geometryUploads;
        f.ui.PrepareFrame(false);
        Check(textScale == 2 && geometryUploads == uploads + 1,
              "DPI/static geometry preparation changed");
        f.ui.PrepareFrame(false);
        Check(geometryUploads == uploads + 1 && mapUploads == maps,
              "unchanged frame rebuilt geometry");

        // Zoom in so view edges lie inside the map and produce a visible outline.
        f.camera.Zoom(8.0f);
        SDL_GPUCommandBuffer commands;
        SDL_GPUTexture target;
        f.ui.UploadFrame(&commands, f.camera, MapExtent, 2560, 1440);
        const int outlines = outlineUploads;
        f.ui.UploadFrame(&commands, f.camera, MapExtent, 2560, 1440);
        Check(outlineUploads == outlines, "unchanged outline reuploaded");
        draws.clear();
        f.ui.Draw(&commands, &target, 2560, 1440, f.camera);
        Check(draws.size() >= 4 && draws[0] == "Solid" && draws[1] == "Map" &&
              draws[2] == "Solid" && draws[3] == "Text", "UI layer order changed");

        f.ui.SetMinimized(true);
        const auto viewport = f.ui.GetWorldViewport(2560, 1440);
        Check(viewport.w == 2560 && viewport.h == 1440, "hidden UI viewport is not full size");
        f.camera.BeginMousePan(200, 200);
        f.camera.MoveMousePan(201, 201);
        const int prepared = textPrepares;
        f.ui.PrepareFrame(true);
        f.ui.UploadFrame(&commands, f.camera, MapExtent, 2560, 1440);
        draws.clear();
        f.ui.Draw(&commands, &target, 2560, 1440, f.camera);
        Check(textPrepares == prepared && draws.size() == 3 &&
              draws[0] == "ui/btn_minimize_normal.png" && draws[1] == "ui/omni_arrows.png" &&
              draws[2] == "ui/dir_circle.png", "hidden UI lost restore/pan sprites");

        f.ui.SetMinimized(false);
        f.window.width = 1920;
        f.window.height = 1080;
        f.ui.PrepareFrame(false);
        Check(mapUploads == maps, "resize rebuilt minimap pixels");
        f.ui.Release();
        f.ui.Release();
        Check(NoGpuObjects() && labelValues.empty(), "UI release leaked resources");
        Check(f.textures.Size() > 0, "UI released shared cache");
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_TAB;
        Check(!f.ui.HandleEvent(event), "released UI processes events");
    }

    std::vector<std::string> DrawTicker(Fixture& f)
    {
        SDL_GPUCommandBuffer commands;
        SDL_GPUTexture target;
        draws.clear();
        f.ui.Draw(&commands, &target, 1280, 720, f.camera);
        std::vector<std::string> ticker;
        for (const auto& image : draws)
        {
            if (image.starts_with("ui/ticker/")) ticker.push_back(image);
        }
        return ticker;
    }

    void ExpectTicker(Fixture& f, bool play, bool pause, std::array<bool, 4> squares)
    {
        std::vector<std::string> expected;
        const auto add = [&](const char* name, bool pressed)
        {
            expected.push_back(std::string("ui/ticker/btn_") + name +
                               (pressed ? "_pressed.png" : "_normal.png"));
        };
        add("pause", pause);
        for (bool lit : squares) add("tickersquare", lit);
        add("play", play);
        Check(DrawTicker(f) == expected, "ticker drew incorrect state textures");
    }

    void TickerState()
    {
        Fixture f;
        f.ui.PrepareFrame(false);
        clockMilliseconds = 1234;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, true, {false, false, false, false});
        clockMilliseconds = 1733;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, true, {false, false, false, false});
        clockMilliseconds = 1734;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, false, {false, false, false, false});
        clockMilliseconds = 2233;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, false, {false, false, false, false});
        clockMilliseconds = 2234;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, true, {false, false, false, false});
        // A long frame skips directly to the current phase instead of drifting.
        clockMilliseconds = 8984;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, false, {false, false, false, false});

        // Changes supplied by the game work without a button click.
        f.ui.Update(f.city, 1);
        ExpectTicker(f, true, false, {true, false, false, false});
        f.ui.Update(f.city, 2);
        ExpectTicker(f, true, false, {true, true, false, false});
        f.ui.Update(f.city, 3);
        ExpectTicker(f, true, false, {true, true, true, false});
        f.ui.Update(f.city, 4);
        ExpectTicker(f, true, false, {true, true, true, true});
        f.ui.Update(f.city, 1);
        ExpectTicker(f, true, false, {true, false, false, false});

        clockMilliseconds = 9000;
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, true, {false, false, false, false});
        clockMilliseconds = 9500;
        f.ui.Update(f.city, 0);
        const auto pause = f.Center(UiButtonIds::TickerPauseButton);
        f.Mouse(SDL_EVENT_MOUSE_MOTION, pause);
        ExpectTicker(f, false, false, {false, false, false, false});
        Check(f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, pause), "pause press not accepted");
        ExpectTicker(f, false, false, {false, false, false, false});
        f.Mouse(SDL_EVENT_MOUSE_BUTTON_UP, pause);
        Check(f.speedRequests == std::vector<int>{0}, "flashing pause stopped accepting clicks");
        f.ui.Update(f.city, 0);
        ExpectTicker(f, false, false, {false, false, false, false});

        const auto speed4 = f.Center(UiButtonIds::TickerSpeedButton4);
        f.Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, speed4);
        ExpectTicker(f, false, false, {false, false, false, false});
        f.Mouse(SDL_EVENT_MOUSE_BUTTON_UP, speed4);
        Check(f.speedRequests == std::vector<int>{0, 4}, "speed square stopped accepting clicks");
        f.ui.Update(f.city, 4);
        ExpectTicker(f, true, false, {true, true, true, true});

        f.ui.SetMinimized(true);
        clockMilliseconds = 10000;
        f.ui.Update(f.city, 0);
        Check(DrawTicker(f).empty(), "ticker visible while UI hidden");
        clockMilliseconds = 10500;
        f.ui.Update(f.city, 0);
        f.ui.SetMinimized(false);
        f.ui.PrepareFrame(false);
        ExpectTicker(f, false, false, {false, false, false, false});

        // Ordinary controls retain their original hover and held feedback.
        f.Center(UiButtonIds::ZoomInButton);
        auto* zoom = f.referenceButtons.Find(UiButtonIds::ZoomInButton);
        const auto point = f.Center(UiButtonIds::ZoomInButton);
        zoom->PointerMove(point[0], point[1]);
        Check(zoom->GetState() == UiButtonState::Highlighted, "ordinary hover feedback changed");
        zoom->PointerDown(point[0], point[1]);
        Check(zoom->GetState() == UiButtonState::Pressed, "ordinary press feedback changed");
    }

    void InitializationFailure()
    {
        SDL_GPUDevice device;
        SDL_Window window;
        TextureManager textures;
        City city;
        UiManager ui;
        failShaderAt = shaderAttempts + 4;
        bool failed = false;
        try
        {
            ui.Initialize(&device, &window, textures, city, {});
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        failShaderAt = 0;
        Check(failed && NoGpuObjects(), "partial initialization leaked");
        ui.Initialize(&device, &window, textures, city, {});
        ui.Release();
        Check(NoGpuObjects(), "retry leaked");
    }
}

int main()
{
    try
    {
        InputRouting();
        Check(NoGpuObjects(), "input fixture leaked");
        RenderingAndLifetime();
        TickerState();
        InitializationFailure();
        Check(imageNames.empty() && labelValues.empty(), "references survived owners");
        std::cout << "PASS: UI manager input, callbacks, resize, DPI, render order, "
                     "minimized pan, ticker states/flashing, cleanup and initialization failure\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
