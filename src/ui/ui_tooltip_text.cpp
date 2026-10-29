#include "ui_tooltip_text.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
    struct Span
    {
        std::string text;
        bool bold;
    };
    using Spans = std::vector<Span>;

    void Append(Spans& spans, std::string_view text, bool bold)
    {
        if (text.empty()) return;
        if (!spans.empty() && spans.back().bold == bold) spans.back().text += text;
        else spans.push_back({std::string(text), bold});
    }

    void Append(Spans& spans, const Spans& other)
    {
        for (const auto& span : other) Append(spans, span.text, span.bold);
    }

    bool Escaped(std::string_view text, std::size_t i)
    {
        return text[i] == '\\' && i + 1 < text.size() &&
            (text[i + 1] == '*' || text[i + 1] == '\\');
    }

    std::size_t NextMarker(std::string_view text, std::size_t i)
    {
        for (; i < text.size(); ++i)
        {
            if (Escaped(text, i)) { ++i; continue; }
            if (text.substr(i, 2) == "**") return i;
        }
        return std::string_view::npos;
    }

    Spans Parse(std::string_view text)
    {
        Spans spans;
        bool bold = false;
        for (std::size_t i = 0; i < text.size();)
        {
            if (Escaped(text, i))
            {
                Append(spans, text.substr(i + 1, 1), bold);
                i += 2;
            }
            else if (text.substr(i, 2) == "**" &&
                     (bold || NextMarker(text, i + 2) != std::string_view::npos))
            {
                bold = !bold;
                i += 2;
            }
            else
            {
                // Normalize Windows line endings before tokenizing.
                if (text[i] == '\r')
                {
                    Append(spans, "\n", bold);
                    i += (i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1;
                }
                else Append(spans, text.substr(i++, 1), bold);
            }
        }
        return spans;
    }

    enum class TokenKind { Word, Space, Newline };
    struct Token { TokenKind kind; Spans spans; };

    std::vector<Token> Tokenize(const Spans& spans)
    {
        std::vector<Token> tokens;
        for (const auto& span : spans)
        for (char c : span.text)
        {
            const auto kind = c == '\n' ? TokenKind::Newline :
                (c == ' ' || c == '\t' ? TokenKind::Space : TokenKind::Word);
            if (tokens.empty() || kind == TokenKind::Newline || tokens.back().kind != kind)
                tokens.push_back({kind, {}});
            if (kind != TokenKind::Newline)
                Append(tokens.back().spans, c == '\t' ? std::string_view("    ") : std::string_view(&c, 1), span.bold);
        }
        return tokens;
    }
}

TooltipTextLayout LayoutTooltipText(std::string_view text, float maxWidth,
                                   float lineHeight, const TooltipTextMeasure& measure)
{
    if (!std::isfinite(maxWidth) || maxWidth < 0 || !std::isfinite(lineHeight) || lineHeight <= 0 || !measure)
        throw std::invalid_argument("Invalid tooltip text layout parameters");
    TooltipTextLayout result;
    const auto tokens = Tokenize(Parse(text));
    if (tokens.empty()) return result;
    Spans line, spaces;
    const auto widthOf = [&](const Spans& spans)
    {
        float width = 0;
        for (const auto& span : spans) width += measure(span.text, span.bold);
        return width;
    };
    const auto tooWide = [&](const Spans& spans)
    {
        // DPI-scaled pixel widths can differ by a few float rounding bits.
        return maxWidth > 0 && widthOf(spans) > maxWidth + 0.001f;
    };
    const auto finishLine = [&]
    {
        float x = 0;
        for (const auto& span : line)
        {
            const float width = measure(span.text, span.bold);
            result.runs.push_back({span.text, span.bold, {x, result.height, width, lineHeight}});
            x += width;
        }
        result.width = std::max(result.width, x);
        result.height += lineHeight;
        line.clear();
    };

    for (const auto& token : tokens)
    {
        if (token.kind == TokenKind::Newline)
        {
            finishLine();
            spaces.clear();
            continue;
        }
        if (token.kind == TokenKind::Space)
        {
            Append(spaces, token.spans);
            continue;
        }

        auto candidate = line;
        Append(candidate, spaces);
        Append(candidate, token.spans);
        spaces.clear();
        if (tooWide(candidate))
        {
            if (!line.empty()) finishLine();
            candidate = token.spans; // Discard whitespace at an automatic wrap.
        }
        if (!tooWide(candidate))
        {
            line = std::move(candidate);
            continue;
        }

        // An individual word can exceed the window width. Break it only at
        // UTF-8 code-point boundaries; never split a multi-byte character.
        for (const auto& span : token.spans)
        for (std::size_t i = 0; i < span.text.size();)
        {
            std::size_t end = i + 1;
            while (end < span.text.size() && (static_cast<unsigned char>(span.text[end]) & 0xc0) == 0x80) ++end;
            const auto character = std::string_view(span.text).substr(i, end - i);
            candidate = line;
            Append(candidate, character, span.bold);
            if (!line.empty() && tooWide(candidate))
            {
                finishLine();
                candidate.clear();
                Append(candidate, character, span.bold);
            }
            line = std::move(candidate);
            i = end;
        }
    }
    finishLine();
    return result;
}
