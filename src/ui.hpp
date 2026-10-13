#pragma once

#include <vector>

struct UiRect
{
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const;
};

struct UiSettings
{
    // Window-coordinate units. The renderer handles drawable-pixel scaling.
    float bottomBarHeight = 92.0f;
    float toolbarWidth = 176.0f;
    // Independent of toolbarWidth; a future slider/drag handle can set this.
    float minimapSize = 224.0f;
};

struct UiLayout
{
    float width = 0, height = 0, scale = 1;
    UiRect world, bottomBar, toolbar, minimap;
    UiRect newsTicker, cityName, population, funds, zoneDemand;
    UiRect toolbarContent, minimapContent;
    bool IsWorldPoint(float x, float y) const;
};

struct UiVertex
{
    float x, y;
    float r, g, b, a;
};

static_assert(sizeof(UiVertex) == 24);

UiLayout CalculateUiLayout(float width, float height, const UiSettings& settings);
std::vector<UiVertex> BuildUiBackground(const UiLayout& layout);
