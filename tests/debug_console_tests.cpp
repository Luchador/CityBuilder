#include "debug_commands.hpp"
#include "city.hpp"
#include "ui_debug_console.hpp"
#include "texture.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

// Exercise the real window, editing, console and command code without a GPU.
// Only font/texture storage, the clock and clipboard are replaced here.
namespace
{
    std::string lastLabelText, clipboard;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    SDL_Event Key(SDL_Keycode key, bool repeat = false)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = key;
        event.key.repeat = repeat;
        return event;
    }
    void Type(UiWindow& window, const char* text)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = text;
        Check(window.HandleKey(event), "Text input was not consumed");
    }
}

Uint64 SDLCALL SDL_GetTicks() { return 0; }
bool SDLCALL SDL_SetClipboardText(const char* text) { clipboard = text; return true; }
char* SDLCALL SDL_GetClipboardText()
{
    auto* text = static_cast<char*>(std::malloc(clipboard.size() + 1));
    if (text) std::memcpy(text, clipboard.c_str(), clipboard.size() + 1);
    return text;
}
void SDLCALL SDL_free(void* pointer) { std::free(pointer); }
Texture::~Texture() = default;
void TextureManager::Release() noexcept { textures.clear(); }
const Texture& TextureManager::Load(std::string_view path)
{
    auto& texture = textures[std::string(path)];
    if (!texture) texture.reset(new Texture(nullptr));
    return *texture;
}
TextLabel::~TextLabel() = default;
void TextLabel::SetText(std::string_view text) { value = text; lastLabelText = text; }
void TextLabel::SetStyle(const TextStyle& next) { style = next; }
void TextLabel::SetBounds(UiRect next) { bounds = next; }
void TextLabel::SetVisible(bool next) { visible = next; }
void TextLabel::SetGroup(std::uint32_t next) { group = next; }
void TextLabel::SetClip(std::optional<UiRect> next) { clip = next; }
void TextLabel::SetHorizontalOffset(float next) { horizontalOffset = next; }
TextLabel& TextRenderer::CreateLabel(std::string_view text, const TextStyle& style)
{
    auto label = std::unique_ptr<TextLabel>(new TextLabel(dirty));
    label->SetText(text);
    label->SetStyle(style);
    labels.push_back(std::move(label));
    return *labels.back();
}
void TextRenderer::Release() noexcept { labels.clear(); }
void TextRenderer::DestroyLabel(TextLabel& label)
{
    std::erase_if(labels, [&](const auto& item) { return item.get() == &label; });
}
SDL_FPoint TextRenderer::MeasureText(std::string_view text, const TextStyle&, float)
{
    return {static_cast<float>(text.size()) * 8, 18};
}

int main()
{
    try
    {
        City city;
        city.funds = 50'000;
        Check(ExecuteDebugCommand("Cooper Airways", &city) == "Added $1,000,000 to city funds." &&
              city.funds == 1'050'000, "Command did not add exactly one million");
        ExecuteDebugCommand("  cOoPeR aIrWaYs\t", &city);
        Check(city.funds == 2'050'000, "Case or outer whitespace was not handled");
        Check(ExecuteDebugCommand("unknown", &city) == "Unknown command." && city.funds == 2'050'000,
              "Unknown command changed funds");
        Check(ExecuteDebugCommand(" \t", &city) == "Enter a command.", "Empty command feedback failed");
        Check(ExecuteDebugCommand("Cooper Airways", nullptr) == "Create a city before using this command.",
              "Missing city was not handled");
        city.funds = std::numeric_limits<std::int64_t>::max() - 999'999;
        const auto before = city.funds;
        ExecuteDebugCommand("Cooper Airways", &city);
        Check(city.funds == before, "Grant overflowed the balance");
        city.funds = std::numeric_limits<std::int64_t>::max() - 1'000'000;
        ExecuteDebugCommand("Cooper Airways", &city);
        Check(city.funds == std::numeric_limits<std::int64_t>::max(), "Maximum valid balance was rejected");
        city.funds = -1'000'100;
        ExecuteDebugCommand("Cooper Airways", &city);
        Check(city.funds == -100, "Negative balance was handled incorrectly");

        TextureManager textures;
        UiWindowSkin skin;
        skin.Load(textures);
        TextRenderer renderer;
        UiWindow window(1, "Debug Console", {20, 20, 560, 196}, renderer, skin);
        int submissions = 0;
        UiDebugConsole console(window, [&](std::string_view command)
        {
            ++submissions;
            return ExecuteDebugCommand(command, &city);
        });
        window.SetFocused(true);
        console.FocusInput();
        Check(window.WantsTextInput(), "Console input was not focused");
        Type(window, "Cooper Airways");
        window.HandleKey(Key(SDLK_RETURN));
        Check(submissions == 1 && city.funds == 999'900 && window.IsOpen() && window.WantsTextInput(),
              "Enter did not submit and keep the console open/focused");
        Check(window.GetText(1).empty() && lastLabelText == "Added $1,000,000 to city funds.",
              "Submission did not clear input and update feedback");
        window.HandleKey(Key(SDLK_RETURN, true));
        Check(submissions == 1, "Held Enter repeated the command");
        Type(window, "bad command");
        window.HandleKey(Key(SDLK_KP_ENTER));
        Check(submissions == 2 && city.funds == 999'900 && lastLabelText == "Unknown command.",
              "Keypad Enter or unknown command feedback failed");

        SDL_Event composition{};
        composition.type = SDL_EVENT_TEXT_EDITING;
        composition.edit.text = "Cooper Airways";
        window.HandleKey(composition);
        window.HandleKey(Key(SDLK_RETURN));
        Check(submissions == 2, "IME composition submitted a command early");
        Type(window, "Cooper Airways");
        window.HandleKey(Key(SDLK_RETURN));
        Check(submissions == 3 && city.funds == 1'999'900, "Committed IME text did not submit");

        window.HandleKey(Key(SDLK_ESCAPE));
        Check(!window.IsOpen(), "Escape did not close the console");
        window.Open();
        window.SetFocused(true);
        console.FocusInput();
        Check(window.WantsTextInput(), "Reopened console did not regain input focus");

        UiWindow form(2, "Existing form", {20, 20, 400, 200}, renderer, skin);
        int accepted = 0, custom = 0;
        form.AddTextBox(1, {0, 0, 300, 32});
        form.AddFooterButton(2, "Accept", [&](UiWindow&) { ++accepted; });
        form.SetFocused(true);
        form.FocusControl(1);
        form.HandleKey(Key(SDLK_RETURN));
        form.HandleKey(Key(SDLK_RETURN, true));
        Check(accepted == 1, "Existing footer Enter behavior changed");
        form.SetTextBoxSubmitHandler(1, [&](UiWindow& panel) { ++custom; panel.Close(); });
        form.HandleKey(Key(SDLK_RETURN));
        Check(accepted == 1 && custom == 1 && !form.IsOpen(), "Submit override also activated the footer");
        std::cout << "PASS: command parsing/funds, overflow, Enter/keypad/repeat, focus, feedback, IME, and existing form behavior\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
