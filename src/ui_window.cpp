#include "ui_window.hpp"

#include "texture.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
    void ValidateRect(UiRect r)
    {
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.w < 0 || r.h < 0)
        {
            throw std::invalid_argument("UI window/control bounds must be finite and nonnegative in size");
        }
    }

}

void UiWindowSkin::Load(TextureManager& textures)
{
    const auto load = [&](const std::string& stem)
    {
        return UiButtonTextures{&textures.Load(stem + "_normal.png"), &textures.Load(stem + "_hov.png"), &textures.Load(stem + "_pressed.png")};
    };

    close = load("ui/btn_close");
    textButton = load("ui/btn_text");

    const auto radio = [&](const std::string& stem)
    {
        const auto* normal = &textures.Load(stem + "_normal.png");
        const auto* hover = &textures.Load(stem + "_hov.png");
    
        return UiButtonTextures{normal, hover, hover};
    };

    radioChecked = radio("ui/btn_radio_checked");
    radioUnchecked = radio("ui/btn_radio_unchecked");
}

UiWindow::UiWindow(UiWindowId identifier, std::string_view caption, UiRect rectangle, TextRenderer& textRenderer, const UiWindowSkin& windowSkin) : id(identifier), renderer(textRenderer), skin(windowSkin), bounds(rectangle), closeButton(skin.close)
{
    if (id == 0) 
    {
        throw std::invalid_argument("Window ID zero is reserved for dock text");
    }

    ValidateRect(bounds);

    if (bounds.w < 160 || bounds.h < 120) 
    {
        throw std::invalid_argument("Window minimum size is 160 x 120");
    }

    TextStyle style;

    style.fontAsset = "fonts/SourceSans3-Semibold.ttf";
    style.size = 18;
    title = &renderer.CreateLabel(caption, style);
    title->SetGroup(id);

    LayoutControls();
}

UiWindow::~UiWindow()
{
    for (const auto& control : controls) 
    {
        if (control->label) renderer.DestroyLabel(*control->label);
    }

    if (title) 
    {
        renderer.DestroyLabel(*title);
    }
}

void UiWindow::SetTitle(std::string_view caption) { title->SetText(caption); }

void UiWindow::SetBounds(UiRect rectangle)
{
    ValidateRect(rectangle);

    if (rectangle.w < 160 || rectangle.h < 120) 
    {
        throw std::invalid_argument("Window minimum size is 160 x 120");
    }

    bounds = rectangle;
    SetScreenSize(screenWidth, screenHeight);
}

void UiWindow::SetScreenSize(float width, float height)
{
    screenWidth = std::max(1.0f, width);
    screenHeight = std::max(1.0f, height);
    // Normal windows stay entirely onscreen. Oversized ones keep the title bar
    // at the top and the right edge/close button visible after a screen resize.
    const float rightmost = screenWidth - bounds.w;
    bounds.x = std::clamp(bounds.x, std::min(0.0f, rightmost), rightmost);
    bounds.y = std::clamp(bounds.y, 0.0f, std::max(0.0f, screenHeight - bounds.h));

    LayoutControls();
}

UiRect UiWindow::GetContentBounds() const
{
    return {bounds.x + Padding, bounds.y + TitleHeight + Padding, bounds.w - Padding * 2, std::max(0.0f, bounds.h - TitleHeight - Padding - FooterHeight)};
}

UiRect UiWindow::GetCloseBounds() const
{
    return {bounds.x + bounds.w - 30, bounds.y + 6, 24, 24};
}

UiRect UiWindow::BoundsOf(const Control& control) const
{
    if (control.footer)
    {
        const float width = std::min(260.0f, bounds.w - Padding * 2);
        return {bounds.x + bounds.w - Padding - width, bounds.y + bounds.h - Padding - 36, width, 36};
    }

    const auto content = GetContentBounds();

    return {content.x + control.local.x, content.y + control.local.y, control.local.w, control.local.h};
}

UiRect UiWindow::GetControlBounds(UiControlId control) const 
{ 
    return BoundsOf(Find(control)); 
}

UiWindow::Control& UiWindow::Find(UiControlId control)
{
    for (auto& item : controls) 
    {
        if (item->id == control) 
        {
            return *item;
        }
    }

    throw std::out_of_range("Unknown window control ID");
}

const UiWindow::Control& UiWindow::Find(UiControlId control) const
{
    for (const auto& item : controls) 
    {
        if (item->id == control) 
        {
            return *item;
        }
    }
    throw std::out_of_range("Unknown window control ID");
}

UiWindow::Control& UiWindow::AddControl(UiControlId controlId, Kind kind, UiRect rectangle)
{
    ValidateRect(rectangle);

    for (const auto& item : controls)
    {
        if (item->id == controlId) 
        {
            throw std::invalid_argument("Window control IDs must be unique within a window");
        }
    }

    auto control = std::make_unique<Control>();
    control->id = controlId;
    control->kind = kind;
    control->local = rectangle;
    controls.push_back(std::move(control));

    return *controls.back();
}

TextLabel& UiWindow::MakeLabel(Control& control, std::string_view text)
{
    control.label = &renderer.CreateLabel(text, control.style);
    control.label->SetGroup(id);
    LayoutControls();

    return *control.label;
}

TextLabel& UiWindow::AddLabel(UiControlId controlId, UiRect rectangle, std::string_view text, TextStyle style)
{
    auto& control = AddControl(controlId, Kind::Label, rectangle);
    control.style = std::move(style);

    return MakeLabel(control, text);
}

void UiWindow::AddImage(UiControlId controlId, UiRect rectangle, const Texture& image)
{
    AddControl(controlId, Kind::Image, rectangle).image = &image;
}

void UiWindow::AddButton(UiControlId controlId, UiRect rectangle, std::string_view text, std::function<void(UiWindow&)> onClick)
{
    auto& control = AddControl(controlId, Kind::Button, rectangle);
    control.button = std::make_unique<UiButton>(skin.textButton);
    control.onClick = std::move(onClick);
    control.style.align = TextAlign::Center;
    control.style.fontAsset = "fonts/SourceSans3-Semibold.ttf";
    control.style.color = {0.07f, 0.13f, 0.20f, 1.0f};

    MakeLabel(control, text);
}

void UiWindow::AddFooterButton(UiControlId controlId, std::string_view text, std::function<void(UiWindow&)> onClick)
{
    for (const auto& item : controls)
    {
        if (item->footer) 
        {
            throw std::logic_error("This window already has a footer button");
        }
    }

    AddButton(controlId, {}, text, std::move(onClick));
    Find(controlId).footer = true;
    LayoutControls();
}

void UiWindow::AddTextBox(UiControlId controlId, UiRect rectangle, std::string_view value, std::size_t maxCharacters)
{
    auto& control = AddControl(controlId, Kind::TextBox, rectangle);
    control.input = UiTextInput(maxCharacters);
    control.input.SetText(value);

    MakeLabel(control, control.input.GetText());
}

void UiWindow::AddRadioButton(UiControlId controlId, std::uint32_t group, UiRect rectangle, std::string_view text, bool checked)
{
    auto& control = AddControl(controlId, Kind::Radio, rectangle);
    control.radioGroup = group;
    control.button = std::make_unique<UiButton>(skin.radioUnchecked);
    MakeLabel(control, text);

    if (checked) 
    {
        SetChecked(controlId);
    }
}

const std::string& UiWindow::GetText(UiControlId controlId) const
{
    const auto& control = Find(controlId);

    if (control.kind != Kind::TextBox) 
    {
        throw std::invalid_argument("GetText requires a text box");
    }

    return control.input.GetText();
}

void UiWindow::SetText(UiControlId controlId, std::string_view text)
{
    auto& control = Find(controlId);

    if (control.kind == Kind::TextBox) 
    { 
        control.input.SetText(text); 
        control.composition.clear(); 
    }
    else if (control.label) 
    {
        control.label->SetText(text);
    }
    else 
    {
        throw std::invalid_argument("This control has no text");
    }
}

bool UiWindow::IsChecked(UiControlId controlId) const
{
    const auto& control = Find(controlId);

    if (control.kind != Kind::Radio) 
    {
        throw std::invalid_argument("IsChecked requires a radio button");
    }

    return control.checked;
}

void UiWindow::SetChecked(UiControlId controlId)
{
    auto& selected = Find(controlId);

    if (selected.kind != Kind::Radio) 
    {
        throw std::invalid_argument("SetChecked requires a radio button");
    }

    for (auto& control : controls)
    {
        if (control->kind == Kind::Radio && control->radioGroup == selected.radioGroup)
        {
            control->checked = control.get() == &selected;
        }
    }
}

void UiWindow::LayoutControls()
{
    closeButton.SetBounds(GetCloseBounds());
    title->SetBounds({bounds.x + 12, bounds.y, std::max(0.0f, bounds.w - 52), TitleHeight});

    for (auto& item : controls)
    {
        auto& control = *item;
        const auto rectangle = BoundsOf(control);
    
        if (control.button) 
        {
            control.button->SetBounds(rectangle);
        }
    
        if (!control.label) 
        {
            continue;
        }
    
        auto textBounds = rectangle;
    
        if (control.kind == Kind::Radio) 
        { 
            textBounds.x += 32; 
            textBounds.w = std::max(0.0f, textBounds.w - 32); 
        }
    
        if (control.kind == Kind::TextBox || control.kind == Kind::Button)
        {
            textBounds.x += 8;
            textBounds.w = std::max(0.0f, textBounds.w - 16);
        }
    
        control.label->SetBounds(textBounds);
        control.label->SetClip(control.footer ? std::optional<UiRect>{bounds} : GetContentBounds());
    }
}

void UiWindow::Open() 
{ 
    open = true; 
}

void UiWindow::Close()
{
    open = shown = false;
    SetFocused(false);
    title->SetVisible(false);

    for (auto& control : controls) 
    {
        if (control->label) 
        {
            control->label->SetVisible(false);
        }
    }
}

void UiWindow::SetFocused(bool value)
{
    if (focused == value) 
    {
        return;
    }

    focused = value;

    if (!value)
    {
        CancelInteraction();

        if (focusedControl) 
        {
            focusedControl->composition.clear();
        }

        focusedControl = nullptr;
    }
}

bool UiWindow::WantsTextInput() const
{
    return open && focused && focusedControl && focusedControl->kind == Kind::TextBox;
}

UiRect UiWindow::GetTextInputBounds() const 
{ 
    return WantsTextInput() ? BoundsOf(*focusedControl) : UiRect{}; 
}

int UiWindow::GetTextInputCursorOffset() const
{
    return WantsTextInput() ? static_cast<int>(8 + focusedControl->caretX - focusedControl->scroll) : 0;
}

bool UiWindow::HitTest(float x, float y) const { return open && bounds.Contains(x, y); }

UiWindow::Control* UiWindow::ControlAt(float x, float y)
{
    for (auto it = controls.rbegin(); it != controls.rend(); ++it)
    {
        auto& control = **it;

        if ((control.kind == Kind::Button || control.kind == Kind::TextBox || control.kind == Kind::Radio) && BoundsOf(control).Contains(x, y) && (control.footer || GetContentBounds().Contains(x, y))) 
        {
            return &control;
        }
    }

    return nullptr;
}

void UiWindow::PlaceCaret(Control& control, float x, bool extend)
{
    const auto& text = control.input.GetText();
    const float target = x - BoundsOf(control).x - 8 + control.scroll;
    std::size_t byte = 0;
    float previous = 0;

    while (byte < text.size())
    {
        const auto next = UiTextInput::Next(text, byte);
        const float width = renderer.MeasureText(std::string_view(text).substr(0, next), control.style, rasterScale).x;
    
        if (target < (previous + width) * 0.5f) 
        {
            break;
        }
    
        previous = width;
        byte = next;
    }

    control.input.MoveTo(byte, extend);
    caretReset = SDL_GetTicks();
}

void UiWindow::PointerDown(float x, float y)
{
    capturing = true; // Even blank content owns the complete press/release pair.
    closePressed = closeButton.PointerDown(x, y);

    if (closePressed) 
    {
        return;
    }

    if (UiRect{bounds.x, bounds.y, bounds.w, TitleHeight}.Contains(x, y))
    {
        dragging = true;
        dragX = x - bounds.x;
        dragY = y - bounds.y;
        return;
    }

    if (focusedControl) 
    {
        focusedControl->composition.clear();
    }

    focusedControl = pressedControl = ControlAt(x, y);

    if (pressedControl && pressedControl->button) 
    {
        pressedControl->button->PointerDown(x, y);
    }

    if (WantsTextInput())
    {
        selecting = true;
        PlaceCaret(*focusedControl, x, false);
    }
}

void UiWindow::PointerMove(float x, float y)
{
    if (dragging)
    {
        bounds.x = x - dragX;
        bounds.y = y - dragY;
        SetScreenSize(screenWidth, screenHeight);
    }

    if (selecting && focusedControl) 
    {
        PlaceCaret(*focusedControl, x, true);
    }

    closeButton.PointerMove(x, y);
    auto* hovered = ControlAt(x, y);

    for (auto& control : controls)
    {
        if (!control->button) 
        {
            continue;
        }

        if (hovered == control.get()) 
        {
            control->button->PointerMove(x, y);
        }
        else 
        {
            control->button->PointerLeave();
        }
    }
}

void UiWindow::Activate(Control& control)
{
    if (control.kind == Kind::Radio) 
    {
        SetChecked(control.id);
    }

    if (control.onClick)
    {
        // Copy so callbacks may safely add more controls/windows or close this one.
        auto callback = control.onClick;
        callback(*this);
    }
}

void UiWindow::PointerUp(float x, float y)
{
    if (!capturing) 
    {
        return;
    }

    auto* clicked = pressedControl;
    const bool activate = clicked && clicked->button && clicked->button->PointerUp(x, y) && ControlAt(x, y) == clicked;
    const bool close = closePressed && closeButton.PointerUp(x, y);
    CancelInteraction();

    if (close) 
    {
        Close();
    }
    else if (activate) 
    {
        Activate(*clicked);
    }
}

void UiWindow::PointerLeave()
{
    closeButton.PointerLeave();
    for (auto& control : controls) 
    {
        if (control->button) 
        {
            control->button->PointerLeave();
        }
    }
}

void UiWindow::CancelInteraction()
{
    capturing = dragging = closePressed = selecting = false;
    pressedControl = nullptr;
    closeButton.CancelPress();

    for (auto& control : controls) 
    {
        if (control->button) 
        {
            control->button->CancelPress();
        }
    }
}

void UiWindow::FocusNext(bool backwards)
{
    std::vector<Control*> focusable;

    for (auto& control : controls)
    {
        if (control->kind == Kind::Button || control->kind == Kind::TextBox || control->kind == Kind::Radio)
        {
            focusable.push_back(control.get());
        }
    }

    if (focusable.empty()) 
    {
        return;
    }

    auto found = std::find(focusable.begin(), focusable.end(), focusedControl);

    std::size_t index = found == focusable.end() ? (backwards ? focusable.size() - 1 : 0) : (static_cast<std::size_t>(found - focusable.begin()) + (backwards ? focusable.size() - 1 : 1)) % focusable.size();
    if (focusedControl) 
    {
        focusedControl->composition.clear();
    }

    focusedControl = focusable[index];
    caretReset = SDL_GetTicks();
}

bool UiWindow::HandleKey(const SDL_Event& event)
{
    if (!BlocksKeyboard()) 
    {
        return false;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT && WantsTextInput())
    {
        focusedControl->composition.clear();
    
        if (event.text.text) 
        {
            focusedControl->input.Insert(event.text.text);
        }

        caretReset = SDL_GetTicks();

        return true;
    }

    if (event.type == SDL_EVENT_TEXT_EDITING && WantsTextInput())
    {
        focusedControl->composition = event.edit.text ? event.edit.text : "";
        return true;
    }

    if (event.type == SDL_EVENT_KEY_UP) 
    {
        return true;
    }

    if (event.type != SDL_EVENT_KEY_DOWN) 
    {
        return false;
    }

    const auto key = event.key.key;
    const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;

    // While an IME composes, it owns editing/Enter/Escape. Committed text arrives
    // through TEXT_INPUT, never by translating physical keycodes ourselves.
    if (WantsTextInput() && !focusedControl->composition.empty()) 
    {
        return true;
    }

    if (key == SDLK_ESCAPE) 
    { 
        if (!event.key.repeat) 
        {
            Close(); 
            return true; 
        }
    }

    if (key == SDLK_TAB) 
    { 
        FocusNext(shift); 
        return true; 
    }

    if (key == SDLK_RETURN && (!focusedControl || WantsTextInput()))
    {
        if (!event.key.repeat)
        {
            for (auto& control : controls) 
            {
                if (control->footer) 
                { 
                    Activate(*control); 
                    break; 
                }
            }
    
        return true;
        }
    }

    if (WantsTextInput())
    {
        auto& input = focusedControl->input;
    
        if (ctrl && key == SDLK_A) 
        {
            input.SelectAll();
        }
        else if (ctrl && (key == SDLK_C || key == SDLK_X))
        {
            const std::string selected(input.SelectedText());
            if (!selected.empty() && SDL_SetClipboardText(selected.c_str()) && key == SDLK_X) 
            {
                input.EraseSelection();
            }
        }
        else if (ctrl && key == SDLK_V)
        {
            if (char* text = SDL_GetClipboardText()) 
            { 
                input.Insert(text); 
                SDL_free(text); 
            }
        }
        else if (key == SDLK_LEFT) input.MoveLeft(shift);
        else if (key == SDLK_RIGHT) input.MoveRight(shift);
        else if (key == SDLK_HOME) input.MoveTo(0, shift);
        else if (key == SDLK_END) input.MoveTo(input.GetText().size(), shift);
        else if (key == SDLK_BACKSPACE) input.Backspace();
        else if (key == SDLK_DELETE) input.Delete();
        caretReset = SDL_GetTicks();
    }
    else if (focusedControl && (key == SDLK_SPACE || key == SDLK_RETURN) && !event.key.repeat)
    {
        Activate(*focusedControl);
    }

    return true; // No camera/game shortcuts while a floating window has focus.
}

void UiWindow::Prepare(bool visible, float scale)
{
    shown = visible && open;
    rasterScale = scale;
    title->SetVisible(shown);
    caretVisible = (SDL_GetTicks() - caretReset) / 500 % 2 == 0;

    for (auto& item : controls)
    {
        auto& control = *item;
        if (control.button && control.kind == Kind::Radio)
        {
            control.button->SetTextures(control.checked ? skin.radioChecked : skin.radioUnchecked);
        }
    
        if (!control.label) 
        {
            continue;
        }
    
        control.label->SetVisible(shown);
        if (!shown || control.kind != Kind::TextBox) 
        {
            continue;
        }
    
        const auto& input = control.input;
        std::string display = input.GetText();
        std::size_t caret = input.Cursor();
    
        if (!control.composition.empty())
        {
            display.replace(input.SelectionBegin(), input.SelectionEnd() - input.SelectionBegin(), control.composition);
            caret = input.SelectionBegin() + control.composition.size();
        }
    
        const auto width = [&](std::size_t end)
        { 
            return renderer.MeasureText(std::string_view(display).substr(0, end), control.style, scale).x; 
        };
    
        control.caretX = width(caret);
        control.selectionStart = width(input.SelectionBegin());
        control.selectionEnd = control.composition.empty() ? width(input.SelectionEnd()) : control.caretX;
        const float available = std::max(1.0f, BoundsOf(control).w - 18);
    
        if (control.caretX < control.scroll) 
        {
            control.scroll = control.caretX;
        }
    
        if (control.caretX > control.scroll + available) 
        {
            control.scroll = control.caretX - available;
        }

        control.scroll = std::clamp(control.scroll, 0.0f, std::max(0.0f, width(display.size()) - available));
        control.label->SetText(display);
        control.label->SetHorizontalOffset(-control.scroll);
    }
}
