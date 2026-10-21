#include "ui_tooltip_text.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    // Distinct regular/bold widths make wrapping and style changes observable.
    float Measure(std::string_view text, bool bold)
    {
        float width = 0;
        for (unsigned char c : text)
            if ((c & 0xc0) != 0x80) width += bold ? 2.0f : 1.0f;
        return width;
    }

    std::string Text(const TooltipTextLayout& layout)
    {
        std::string text;
        for (const auto& run : layout.runs) text += run.text;
        return text;
    }

    void CheckFits(const TooltipTextLayout& layout, float width)
    {
        for (const auto& run : layout.runs)
        {
            Check(run.bounds.x >= 0 && run.bounds.y >= 0, "negative run position");
            Check(run.bounds.x + run.bounds.w <= width, "run exceeds wrap width");
            Check(run.bounds.y + run.bounds.h <= layout.height, "run exceeds text height");
        }
    }
}

int main()
{
    try
    {
        auto text = LayoutTooltipText("Build **schools** here.", 0, 10, Measure);
        Check(Text(text) == "Build schools here.", "markup must not be displayed");
        Check(text.runs.size() == 3 && !text.runs[0].bold && text.runs[1].bold && !text.runs[2].bold,
              "mixed bold/normal styles");
        Check(text.runs[1].bounds.x == 6 && text.runs[2].bounds.x == 20, "bold width affects following runs");

        text = LayoutTooltipText("**Title**\r\n\r\nNormal text", 0, 10, Measure);
        Check(text.height == 30 && text.runs.size() == 2 && text.runs[1].bounds.y == 20,
              "Windows line endings and empty lines");
        Check(LayoutTooltipText("line\n", 0, 10, Measure).height == 20, "trailing explicit newline");
        Check(LayoutTooltipText("", 0, 10, Measure).runs.empty(), "empty tooltip");
        text = LayoutTooltipText("An unmatched **marker", 0, 10, Measure);
        Check(Text(text) == "An unmatched **marker" && !text.runs.front().bold, "unpaired markers stay literal");
        text = LayoutTooltipText(R"(\*\*literal\*\* and **bold** \\)", 0, 10, Measure);
        Check(Text(text) == "**literal** and bold \\", "escaped asterisks and backslash");

        text = LayoutTooltipText("a **bb** c", 5, 10, Measure);
        Check(text.height == 30 && text.runs[1].bounds.y == 10 && text.runs[2].bounds.y == 20,
              "bold word wraps as a whole");
        CheckFits(text, 5);
        text = LayoutTooltipText("xx a**b**c", 4, 10, Measure);
        Check(text.runs.size() == 4 && text.runs[1].bounds.y == 10 && text.height == 20,
              "formatting does not introduce word breaks");
        CheckFits(text, 4);
        text = LayoutTooltipText("**abcdefgh**", 5, 10, Measure);
        Check(text.height == 40 && Text(text) == "abcdefgh", "overlong bold word wraps without losing text");
        CheckFits(text, 5);
        text = LayoutTooltipText("caf\xc3\xa9" "caf\xc3\xa9", 4, 10, Measure);
        Check(text.runs.size() == 2 && text.runs[0].text == "caf\xc3\xa9" && text.runs[1].text == "caf\xc3\xa9",
              "UTF-8 characters remain intact across wraps");
        CheckFits(text, 4);
        // A window narrower than one glyph must still make progress.
        text = LayoutTooltipText("**abc**", 1, 10, Measure);
        Check(text.height == 30 && Text(text) == "abc", "single glyph overflow terminates");
        std::cout << "PASS: tooltip emphasis, escapes, line breaks, word wrapping and UTF-8 boundaries\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
