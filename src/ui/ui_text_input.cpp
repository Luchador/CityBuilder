#include "ui_text_input.hpp"

#include <algorithm>
#include <cstdint>

namespace
{
    bool Continuation(unsigned char c) { return (c & 0xc0) == 0x80; }

    std::size_t Previous(std::string_view text, std::size_t byte)
    {
        if (byte == 0)
        {
            return 0;
        }
    
        --byte;
    
        while (byte > 0 && Continuation(static_cast<unsigned char>(text[byte]))) 
        {
            --byte;
        }

        return byte;
    }

    std::size_t Count(std::string_view text)
    {
        std::size_t count = 0;
        for (unsigned char c : text) 
        {
            if (!Continuation(c)) 
            {
                ++count;
            }
        }
    
        return count;
    }

    std::string Clean(std::string_view text, std::size_t limit)
    {
        std::string result;

        for (std::size_t i = 0, count = 0; i < text.size() && count < limit;)
        {
            const auto first = static_cast<unsigned char>(text[i]);
            const std::size_t n = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    
            if (n == 0 || i + n > text.size()) 
            { 
                ++i; 
                continue; 
            }

            std::uint32_t cp = first & (n == 1 ? 0x7f : (1u << (7 - n)) - 1);

            bool valid = true;
    
            for (std::size_t j = 1; j < n; ++j)
            {
                const auto c = static_cast<unsigned char>(text[i + j]);
                valid = valid && Continuation(c);
                cp = (cp << 6) | (c & 0x3f);
            }
    
            valid = valid && (n < 3 || cp >= (n == 3 ? 0x800u : 0x10000u)) && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
            if (!valid) 
            { 
                ++i; 
                continue; 
            }
    
            // Text boxes are single-line. Drop control characters from pasted data.
            if (cp >= 0x20 && !(cp >= 0x7f && cp <= 0x9f) && cp != 0x2028 && cp != 0x2029)
            {
                result.append(text.substr(i, n));
                ++count;
            }
    
            i += n;
        }

        return result;
    }
}

std::size_t UiTextInput::Next(std::string_view text, std::size_t byte)
{
    if (byte >= text.size()) 
    {
        return text.size();
    }

    ++byte;

    while (byte < text.size() && Continuation(static_cast<unsigned char>(text[byte]))) 
    {
        ++byte;
    }

    return byte;
}

std::size_t UiTextInput::SelectionBegin() const 
{ 
    return std::min(cursor, anchor); 
}

std::size_t UiTextInput::SelectionEnd() const 
{ 
    return std::max(cursor, anchor); 
}

std::string_view UiTextInput::SelectedText() const
{
    return std::string_view(value).substr(SelectionBegin(), SelectionEnd() - SelectionBegin());
}

void UiTextInput::SetText(std::string_view text)
{
    value = Clean(text, maxCharacters);
    cursor = anchor = value.size();
}

void UiTextInput::EraseSelection()
{
    const auto begin = SelectionBegin();
    value.erase(begin, SelectionEnd() - begin);
    cursor = anchor = begin;
}

void UiTextInput::Insert(std::string_view text)
{
    const auto available = maxCharacters - (Count(value) - Count(SelectedText()));
    const auto addition = Clean(text, available);

    if (addition.empty()) 
    {
        return;
    }

    EraseSelection();
    value.insert(cursor, addition);
    cursor += addition.size();
    anchor = cursor;
}

void UiTextInput::MoveTo(std::size_t byte, bool extend)
{
    cursor = std::min(byte, value.size());

    while (cursor > 0 && cursor < value.size() && Continuation(static_cast<unsigned char>(value[cursor]))) 
    {
        --cursor;
    }

    if (!extend) 
    {
        anchor = cursor;
    }
}

void UiTextInput::MoveLeft(bool extend)
{
    MoveTo(!extend && cursor != anchor ? SelectionBegin() : Previous(value, cursor), extend);
}

void UiTextInput::MoveRight(bool extend)
{
    MoveTo(!extend && cursor != anchor ? SelectionEnd() : Next(value, cursor), extend);
}

void UiTextInput::Backspace()
{
    if (cursor == anchor) 
    {
        MoveLeft(true);
    }

    EraseSelection();
}

void UiTextInput::Delete()
{
    if (cursor == anchor) 
    {
        MoveRight(true);
    }

    EraseSelection();
}
