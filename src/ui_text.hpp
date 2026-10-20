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
    void SetLayout(const UiLayout& layout);

private:
    // Borrowed from TextRenderer. Release the renderer after the UI stops using these.
    TextLabel* news = nullptr;
    std::array<TextLabel*, 4> captions{};
    std::array<TextLabel*, 4> values{};
};
