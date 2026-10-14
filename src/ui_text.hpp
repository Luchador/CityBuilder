#pragma once

#include <array>
#include <string>

#include "text.hpp"

// Presentation strings only. Simulation code can format and replace them later.
struct CityStatusText
{
    std::string news = "Welcome to New Haven! Your city's future starts here.";
    std::string cityName = "New Haven";
    std::string population = "12,500";
    std::string funds = "$250,000";
    std::string date = "Jan 1950";
};

class BottomBarText
{
public:
    void Initialize(TextRenderer& renderer);
    void SetValues(const CityStatusText& values);
    void SetLayout(const UiLayout& layout);

private:
    // Borrowed from TextRenderer. Release the renderer after the UI stops using these.
    TextLabel* news = nullptr;
    std::array<TextLabel*, 4> captions{};
    std::array<TextLabel*, 4> values{};
};
