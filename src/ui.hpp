#pragma once

#include <vector>

struct UiRect
{
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const;
};

struct UiSettings
{
    // Height of the bottom bar, or width of the toolbar.
    float barSlimDimension = 92.0f;

    float minimapSize = 224.0f;
};

struct UiLayout
{
    float width = 0, height = 0, scale = 1;
    UiRect world, bottomBar, toolbar, minimapPanel;
    UiRect newsTicker, cityName, population, funds, date, zoneDemand;
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
