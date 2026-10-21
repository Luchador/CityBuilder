#pragma once

#include "ui.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct TooltipTextRun
{
    std::string text;
    bool bold = false;
    UiRect bounds; // Relative to the tooltip's text area.
};

struct TooltipTextLayout
{
    std::vector<TooltipTextRun> runs;
    float width = 0, height = 0;
};

// Small, tooltip-specific markup: **bold**, explicit newlines, and backslash
// escapes for asterisks/backslashes. Unpaired ** stays literal; this is not a
// full Markdown parser. Width zero disables automatic wrapping.
using TooltipTextMeasure = std::function<float(std::string_view, bool bold)>;
TooltipTextLayout LayoutTooltipText(std::string_view text, float maxWidth,
                                   float lineHeight, const TooltipTextMeasure& measure);
