#include "slider_input.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace renderkit
{
namespace
{
    template<class T> bool ParseValue(std::string_view text,T& value)
    {
        const auto first=text.find_first_not_of(" \t\r\n");
        if(first==std::string_view::npos)return false;
        text=text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
        if(text.front()=='+')text.remove_prefix(1);
        if(text.empty())return false;
        const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
        return result.ec==std::errc{} && result.ptr==text.data()+text.size() && std::isfinite(value);
    }
}
void SliderInput::Reset()
{
    editingId=0;editingValue=nullptr;lastFrame=-1;focus=false;invalid=false;
}
template<class T> bool SliderInput::Draw(const char* label,T* value,T minimum,T maximum,
                                        const char* format,ImGuiSliderFlags flags)
{
    const auto id=ImGui::GetID(label);
    const int frame=ImGui::GetFrameCount();
    if(editingId && (lastFrame<frame-1 || (editingId==id && editingValue!=value)))Reset();
    if(editingId==id)
    {
        lastFrame=frame;
        // A distinct input ID prevents an active slider from reusing ImGui's
        // cached text from a previous edit of this setting.
        ImGui::PushID("SliderText");
        const bool requestingFocus=std::exchange(focus,false);
        if(requestingFocus)ImGui::SetKeyboardFocusHere();
        if(invalid)
        {
            ImGui::PushStyleColor(ImGuiCol_Border,{.95f,.35f,.25f,1});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.0f);
        }
        const bool enter=ImGui::InputText(label,text,sizeof(text),
            ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopID();
        if(invalid){ImGui::PopStyleVar();ImGui::PopStyleColor();}
        ImGui::SetItemTooltip(invalid?"Enter a valid finite number. Enter confirms; Escape cancels.":
                                     "Enter confirms; Escape or clicking elsewhere cancels. Values are clamped to the slider range.");
        if(enter)
        {
            T parsed{};
            if(!ParseValue(text,parsed)){invalid=true;focus=true;return false;}
            parsed=std::clamp(parsed,minimum,maximum);
            const bool changed=parsed!=*value;*value=parsed;Reset();return changed;
        }
        if((ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) ||
           (!requestingFocus && !ImGui::IsItemActive()))Reset();
        return false;
    }

    const T previous=*value;
    const auto start=ImGui::GetCursorScreenPos();
    const float width=ImGui::CalcItemWidth(),height=ImGui::GetFrameHeight();
    char displayed[64];std::snprintf(displayed,sizeof(displayed),format,previous);
    const auto size=ImGui::CalcTextSize(displayed);
    const ImVec2 numberMin{start.x+std::max(0.0f,(width-size.x)*.5f-3),start.y+(height-size.y)*.5f-2};
    const ImVec2 numberMax{start.x+std::min(width,(width+size.x)*.5f+3),start.y+(height+size.y)*.5f+2};
    bool changed;
    // Disable native immediate text edits; our text field commits only on Enter.
    if constexpr(std::is_same_v<T,float>)changed=ImGui::SliderFloat(label,value,minimum,maximum,format,flags|ImGuiSliderFlags_NoInput);
    else changed=ImGui::SliderInt(label,value,minimum,maximum,format,flags|ImGuiSliderFlags_NoInput);
    const bool overNumber=ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(numberMin,numberMax);
    const bool mouseEdit=ImGui::IsItemClicked() && (overNumber || (ImGui::GetIO().KeyCtrl &&
        ImGui::IsMouseHoveringRect(start,{start.x+width,start.y+height})));
    const bool keyboardEdit=ImGui::IsItemFocused() && ImGui::IsItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter,false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter,false));
    if(overNumber)ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::SetItemTooltip("Drag the slider, or click its number to type. Enter confirms; Escape cancels.");
    if(mouseEdit || keyboardEdit)
    {
        // SliderScalar may have moved the thumb on mouse-down. Restore the
        // untouched value before starting input; do not issue a preview change.
        *value=previous;editingId=id;editingValue=value;lastFrame=frame;focus=true;invalid=false;
        const auto result=std::to_chars(text,text+sizeof(text)-1,previous);
        *result.ptr=0; // Round-trip precision, independent of the display format/units.
        return false;
    }
    return changed;
}
bool SliderInput::Float(const char* label,float* value,float minimum,float maximum,const char* format,ImGuiSliderFlags flags)
{return Draw(label,value,minimum,maximum,format,flags);}
bool SliderInput::Int(const char* label,int* value,int minimum,int maximum,const char* format,ImGuiSliderFlags flags)
{return Draw(label,value,minimum,maximum,format,flags);}
}
