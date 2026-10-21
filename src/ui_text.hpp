#pragma once

#include <array>
#include <string>

#include "city.hpp"
#include "text.hpp"

class BottomBarText
{
public:
    void Initialize(TextRenderer& renderer);
    void UpdateText(const City& city);
    void SetToolStatus(std::string_view text) { toolStatus = text; }
    void SetLayout(const UiLayout& layout);

private:
    // Borrowed from TextRenderer. Release the renderer after the UI stops using these.
    TextLabel* news = nullptr;
    std::string toolStatus;
    std::array<TextLabel*, 4> captions{};
    std::array<TextLabel*, 4> values{};
};
