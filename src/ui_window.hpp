#pragma once

#include "text.hpp"
#include "ui_button.hpp"
#include "ui_text_input.hpp"

#include <functional>
#include <memory>
#include <optional>

class UiImageRenderer;
class TextureManager;

using UiWindowId = std::uint32_t;
using UiControlId = std::uint32_t;

struct UiWindowSkin
{
    UiButtonTextures close, textButton, radioChecked, radioUnchecked;
    void Load(TextureManager& textures);
};

// UiManager owns windows. Positions/sizes use SDL logical window coordinates.
// Child bounds are relative to the content area's upper-left corner; the footer
// is anchored separately. The shared text renderer and skin outlive each window.
class UiWindow
{
public:
    UiWindow(UiWindowId id, std::string_view title, UiRect bounds,
             TextRenderer& renderer, const UiWindowSkin& skin);
    ~UiWindow();
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;

    UiWindowId GetId() const { return id; }
    void SetTitle(std::string_view title);
    void SetBounds(UiRect bounds);
    const UiRect& GetBounds() const { return bounds; }
    UiRect GetContentBounds() const;
    UiRect GetCloseBounds() const;
    UiRect GetControlBounds(UiControlId control) const;
    void SetScreenSize(float width, float height);
    void Open();
    void Close(); // Keeps this instance and its control values available to reopen.
    bool IsOpen() const { return open; }
    // Disables user dismissal (X and Escape). Application callbacks may still
    // call Close(), for example after accepting a required form.
    void SetClosable(bool value);
    bool IsClosable() const { return closable; }

    TextLabel& AddLabel(UiControlId id, UiRect bounds, std::string_view text, TextStyle style = {});
    void AddImage(UiControlId id, UiRect bounds, const Texture& image);
    void AddButton(UiControlId id, UiRect bounds, std::string_view text,
                   std::function<void(UiWindow&)> onClick);
    void AddFooterButton(UiControlId id, std::string_view text,
                         std::function<void(UiWindow&)> onClick);
    void AddTextBox(UiControlId id, UiRect bounds, std::string_view value = {}, std::size_t maxCharacters = 128);
    void AddRadioButton(UiControlId id, std::uint32_t group, UiRect bounds,
                        std::string_view text, bool checked = false);
    const std::string& GetText(UiControlId id) const;
    void SetText(UiControlId id, std::string_view text);
    void SetTooltip(UiControlId id, std::string_view text);
    void SetCloseTooltip(std::string_view text) { closeButton.SetTooltip(text); }
    bool IsChecked(UiControlId id) const;
    void SetChecked(UiControlId id);

    // Manager-facing input/render operations. Only the topmost hit window gets
    // pointer input; keyboard events go to the focused window.
    void SetFocused(bool value);
    bool BlocksKeyboard() const { return focused && open; }
    bool WantsTextInput() const;
    const void* GetTextInputIdentity() const { return WantsTextInput() ? focusedControl : nullptr; }
    UiRect GetTextInputBounds() const;
    int GetTextInputCursorOffset() const;
    bool HitTest(float x, float y) const;
    const UiButton* HoveredButton() const;
    bool HasCapture() const { return capturing; }
    void PointerDown(float x, float y);
    void PointerMove(float x, float y);
    void PointerUp(float x, float y);
    void PointerLeave();
    void CancelInteraction();
    bool HandleKey(const SDL_Event& event);
    void Prepare(bool visible, float rasterScale);
    void AppendGeometry(std::vector<UiVertex>& mesh) const;
    void DrawImages(UiImageRenderer& renderer, SDL_GPUCommandBuffer* commands,
                    SDL_GPURenderPass* pass, float width, float height,
                    Uint32 pixelWidth, Uint32 pixelHeight) const;

private:
    enum class Kind { Label, Image, Button, TextBox, Radio };
    struct Control
    {
        UiControlId id = 0;
        Kind kind = Kind::Label;
        UiRect local;
        TextLabel* label = nullptr;
        TextStyle style;
        std::unique_ptr<UiButton> button;
        const Texture* image = nullptr;
        std::function<void(UiWindow&)> onClick;
        UiTextInput input;
        std::uint32_t radioGroup = 0;
        bool checked = false, footer = false;
        float scroll = 0, caretX = 0, selectionStart = 0, selectionEnd = 0;
        std::string composition;
    };
    Control& AddControl(UiControlId id, Kind kind, UiRect bounds);
    Control& Find(UiControlId id);
    const Control& Find(UiControlId id) const;
    TextLabel& MakeLabel(Control& control, std::string_view text);
    UiRect BoundsOf(const Control& control) const;
    Control* ControlAt(float x, float y);
    void LayoutControls();
    void FocusNext(bool backwards);
    void Activate(Control& control);
    void PlaceCaret(Control& control, float x, bool extend);

    UiWindowId id;
    TextRenderer& renderer;
    const UiWindowSkin& skin;
    UiRect bounds;
    float screenWidth = 1280, screenHeight = 720, rasterScale = 1;
    TextLabel* title = nullptr;
    UiButton closeButton;
    std::vector<std::unique_ptr<Control>> controls;
    Control* focusedControl = nullptr;
    Control* pressedControl = nullptr;
    bool open = true, focused = false, shown = true;
    bool closable = true;
    bool capturing = false, dragging = false, closePressed = false, selecting = false;
    float dragX = 0, dragY = 0;
    bool caretVisible = false;
    Uint64 caretReset = 0;
    static constexpr float TitleHeight = 36, Padding = 16, FooterHeight = 60;
};
