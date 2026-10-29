#pragma once
#include <imgui.h>

namespace renderkit
{
    // One edit session per editor. Text stays separate from settings until Enter.
    class SliderInput
    {
    public:
        bool Float(const char* label,float* value,float minimum,float maximum,
                   const char* format="%.3f",ImGuiSliderFlags flags=0);
        bool Int(const char* label,int* value,int minimum,int maximum,
                 const char* format="%d",ImGuiSliderFlags flags=0);
        void Reset();
    private:
        template<class T> bool Draw(const char* label,T* value,T minimum,T maximum,
                                   const char* format,ImGuiSliderFlags flags);
        ImGuiID editingId=0;
        const void* editingValue=nullptr;
        int lastFrame=-1;
        bool focus=false,invalid=false;
        char text[64]{};
    };
}
