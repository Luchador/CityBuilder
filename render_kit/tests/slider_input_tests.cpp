#include "slider_input.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    struct Harness
    {
        renderkit::SliderInput input;
        float value=.37f,other=.61f;
        int integer=64,changes=0;
        bool useInt=false,disabled=false,useOther=false,show=true,logarithmic=false;
        bool active=false;
        Harness()
        {
            ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={600,300};io.DeltaTime=1.0f/60;
            io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.ConfigInputTrickleEventQueue=false;
            io.IniFilename=nullptr;io.Fonts->AddFontDefault();
            unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
            io.Fonts->SetTexID(1);
            Frame();Frame();
        }
        ~Harness(){ImGui::DestroyContext();}
        void Frame()
        {
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,300});
            ImGui::Begin("Test",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
            ImGui::SetCursorScreenPos({20,20});ImGui::SetNextItemWidth(240);
            ImGui::BeginDisabled(disabled);
            if(show)
            {
                bool changed;
                if(useInt)changed=input.Int("Value",&integer,16,256);
                else changed=input.Float("Value",useOther?&other:&value,logarithmic?.001f:0.0f,logarithmic?10000.0f:1.0f,
                    logarithmic?"%.3f units":"%.3f",logarithmic?ImGuiSliderFlags_Logarithmic:0);
                if(changed)++changes;active=ImGui::IsItemActive();
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos({400,100});ImGui::Button("Elsewhere");
            ImGui::End();ImGui::Render();
        }
        void Click(float x,float y=29)
        {
            auto& io=ImGui::GetIO();io.AddMousePosEvent(x,y);Frame();
            io.AddMouseButtonEvent(0,true);Frame();io.AddMouseButtonEvent(0,false);Frame();Frame();
        }
        void Key(ImGuiKey key)
        {auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);Frame();io.AddKeyEvent(key,false);Frame();}
        void Type(const char* text)
        {
            auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);Frame();
            io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);Frame();
            Key(ImGuiKey_Backspace);
            io.AddInputCharactersUTF8(text);Frame();
        }
    };
}
int main()
{
    try
    {
        Harness h;
        h.Click(140);Check(h.active && h.value==.37f && !h.changes,"Clicking the number changed its value or failed to focus input");
        h.Type("0.1234567");Check(h.value==.37f && !h.changes,"Typing must not change the setting or trigger preview");
        h.Key(ImGuiKey_Enter);Check(std::abs(h.value-.1234567f)<1e-8f && h.changes==1,"Enter did not commit precise float input once");
        const float precise=h.value;h.Click(140);h.Key(ImGuiKey_Enter);
        Check(h.value==precise && h.changes==1,"Confirming unchanged text lost precision");
        h.Click(140);h.Type(".9");h.Key(ImGuiKey_Escape);
        Check(h.value==precise && h.changes==1,"Escape did not cancel");
        h.Click(140);h.Type(".8");h.Click(420,110);h.Key(ImGuiKey_Enter);
        Check(h.value==precise && h.changes==1,"Clicking elsewhere committed unfinished text");
        h.Click(140);h.Type("5");h.Key(ImGuiKey_Enter);
        Check(h.value==1 && h.changes==2,"Float input did not clamp to maximum");
        h.Click(140);h.Type("-5");h.Key(ImGuiKey_Enter);Check(h.value==0,"Float input did not clamp to minimum");
        for(const char* invalid:{"nan","inf","1e999","1.2junk","","--1"})
        {
            h.Click(140);h.Type(invalid);const auto changes=h.changes;h.Key(ImGuiKey_Enter);
            if(h.value!=0 || h.changes!=changes)throw std::runtime_error(std::string("Invalid input '")+invalid+
                "' changed the setting to "+std::to_string(h.value)+" (changes "+std::to_string(changes)+" -> "+std::to_string(h.changes)+")");
            h.Type("0.25");h.Key(ImGuiKey_Enter);Check(h.value==.25f,"Invalid input could not be corrected");
            h.value=0;
        }
        h.disabled=true;h.Click(140);h.Type("0.8");h.Key(ImGuiKey_Enter);
        Check(h.value==0,"Disabled slider accepted input");h.disabled=false;h.Frame();
        h.Click(230);Check(h.value>.8f,"Slider track stopped working");
        auto& io=ImGui::GetIO();io.AddMousePosEvent(230,29);io.AddMouseButtonEvent(0,true);h.Frame();
        io.AddMousePosEvent(65,29);h.Frame();io.AddMouseButtonEvent(0,false);h.Frame();
        Check(h.value<.3f,"Dragging across the numeric region stopped working");
        h.Click(140);h.Type(".93");h.useOther=true;h.Frame();h.Key(ImGuiKey_Enter);
        Check(h.other==.61f,"Pending edit leaked into a different material/target");h.useOther=false;h.Frame();
        h.Click(140);h.Type(".75");const auto beforeHide=h.value;h.show=false;h.Frame();h.Frame();h.show=true;h.Frame();
        h.Key(ImGuiKey_Enter);Check(h.value==beforeHide,"Hidden control restored a stale edit");
        h.input.Reset();h.value=.456789f;h.Frame();h.Click(140);h.Key(ImGuiKey_Enter);
        Check(h.value==.456789f,"Reopening after an external value change reused stale text");
        h.input.Reset();h.logarithmic=true;h.value=1;h.Frame();h.Click(140);h.Type("2.5e-2");h.Key(ImGuiKey_KeypadEnter);
        Check(h.value==.025f,"Logarithmic slider or keypad Enter failed");
        h.Click(140);h.Type("20000");h.Key(ImGuiKey_Enter);Check(h.value==10000,"Logarithmic input did not clamp");
        h.input.Reset();h.useInt=true;h.Frame();h.Click(140);h.Type("128");
        Check(h.integer==64,"Integer changed before confirmation");h.Key(ImGuiKey_Enter);Check(h.integer==128,"Integer failed to commit");
        h.Click(140);h.Type("12.5");h.Key(ImGuiKey_Enter);Check(h.integer==128,"Fractional integer input was accepted");
        h.Type("999");h.Key(ImGuiKey_Enter);Check(h.integer==256,"Integer maximum was not enforced");
        h.Click(140);h.Type("-10");h.Key(ImGuiKey_Enter);Check(h.integer==16,"Integer minimum was not enforced");
        std::cout<<"PASS: slider numeric clicks, Enter/keypad commit, Escape/outside cancel, precision, validation/clamping, disabled/hidden controls, material targets and native dragging\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
