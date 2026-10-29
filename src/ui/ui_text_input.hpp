#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Single-line UTF-8 editing state, independent of SDL, layout, and rendering.
// Cursor/selection offsets are bytes at code-point boundaries. Combining marks
// are separate editing steps; full grapheme navigation can be added later.
class UiTextInput
{
public:
    explicit UiTextInput(std::size_t limit = 128) : maxCharacters(limit) {}
    void SetText(std::string_view text);
    void Insert(std::string_view text);
    void Backspace();
    void Delete();
    void MoveLeft(bool extend = false);
    void MoveRight(bool extend = false);
    void MoveTo(std::size_t byte, bool extend = false);
    void SelectAll() { anchor = 0; cursor = value.size(); }
    void EraseSelection();
    const std::string& GetText() const { return value; }
    std::size_t Cursor() const { return cursor; }
    std::size_t SelectionBegin() const;
    std::size_t SelectionEnd() const;
    std::string_view SelectedText() const;
    static std::size_t Next(std::string_view text, std::size_t byte);

private:
    std::string value;
    std::size_t cursor = 0, anchor = 0;
    std::size_t maxCharacters;
};
