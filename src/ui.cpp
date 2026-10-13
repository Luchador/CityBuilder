#include "ui.hpp"

#include <algorithm>

bool UiRect::Contains(float px, float py) const
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

bool UiLayout::IsWorldPoint(float x, float y) const
{
    return world.Contains(x, y) && !minimap.Contains(x, y);
}

namespace
{
    UiRect Inset(UiRect r, float amount)
    {
        return {r.x + amount, r.y + amount, std::max(0.0f, r.w - 2 * amount), std::max(0.0f, r.h - 2 * amount)};
    }

    struct Color { float r, g, b; };

    void Quad(std::vector<UiVertex>& vertices, UiRect r, Color top, Color bottom)
    {
        if (r.w <= 0 || r.h <= 0) 
        {
            return;
        }
    
        const UiVertex a{r.x, r.y, top.r, top.g, top.b, 1};
        const UiVertex b{r.x + r.w, r.y, top.r, top.g, top.b, 1};
        const UiVertex c{r.x, r.y + r.h, bottom.r, bottom.g, bottom.b, 1};
        const UiVertex d{r.x + r.w, r.y + r.h, bottom.r, bottom.g, bottom.b, 1};

        vertices.insert(vertices.end(), {a, b, c, b, d, c});
    }

    void Solid(std::vector<UiVertex>& vertices, UiRect r, Color color)
    {
        Quad(vertices, r, color, color);
    }

    void Frame(std::vector<UiVertex>& vertices, UiRect r, float edge, bool recessed)
    {
        const Color light{0.72f, 0.75f, 0.74f};
        const Color shadow{0.17f, 0.22f, 0.24f};
        const Color top = recessed ? shadow : light;
        const Color bottom = recessed ? light : shadow;
        Solid(vertices, {r.x, r.y, r.w, edge}, top);
        Solid(vertices, {r.x, r.y + edge, edge, r.h - edge}, top);
        Solid(vertices, {r.x, r.y + r.h - edge, r.w, edge}, bottom);
        Solid(vertices, {r.x + r.w - edge, r.y, edge, r.h}, bottom);
    }

    void Well(std::vector<UiVertex>& vertices, UiRect r, float scale)
    {
        Quad(vertices, r, {0.12f, 0.17f, 0.19f}, {0.17f, 0.23f, 0.25f});
        Frame(vertices, r, scale, true);
    }
}

UiLayout CalculateUiLayout(float width, float height, const UiSettings& settings)
{
    UiLayout layout;

    layout.width = std::max(width, 1.0f);
    layout.height = std::max(height, 1.0f);
    
    // Normally 1. Window minimum size is 800x480; retain safe geometry even
    // if a platform temporarily reports a smaller non-minimized window.
    const float s = std::min({1.0f, layout.width / 800.0f, layout.height / 480.0f});
    layout.scale = s;
    const float bottom = std::clamp(settings.bottomBarHeight, 72.0f, 128.0f) * s;
    const float right = std::clamp(settings.toolbarWidth, 128.0f, 240.0f) * s;

    // Reserve useful space for city information and toolbar controls.
    const float maxMap = std::min({512.0f * s, layout.width - 480.0f * s, layout.height - 220.0f * s});
    const float map = std::clamp(settings.minimapSize * s, 128.0f * s, maxMap);
    layout.world = {0, 0, layout.width - right, layout.height - bottom};
    layout.bottomBar = {0, layout.height - bottom, layout.width, bottom};
    layout.toolbar = {layout.width - right, 0, right, layout.height - bottom};
    layout.minimap = {layout.width - map, layout.height - map, map, map};
    const float gap = 8.0f * s;
    // Both the bar and toolbar stop their content before the minimap frame.
    const float contentWidth = std::min(layout.minimap.x, layout.toolbar.x) - 2 * gap;
    layout.newsTicker = {gap, layout.bottomBar.y + gap, contentWidth, 28.0f * s};
    const float rowY = layout.newsTicker.y + layout.newsTicker.h + gap;
    const float rowH = layout.height - gap - rowY;
    const float demandW = 80.0f * s;
    const float statsW = contentWidth - demandW - 3 * gap;

    layout.cityName = {gap, rowY, statsW * 0.42f, rowH};
    layout.population = {layout.cityName.x + layout.cityName.w + gap, rowY, statsW * 0.27f, rowH};
    layout.funds = {layout.population.x + layout.population.w + gap, rowY, statsW * 0.31f, rowH};
    layout.zoneDemand = {layout.funds.x + layout.funds.w + gap, rowY, demandW, rowH};
    layout.toolbarContent = {layout.toolbar.x + gap, gap, right - 2 * gap, std::min(layout.minimap.y, layout.world.h) - 2 * gap};
    layout.minimapContent = Inset(layout.minimap, gap);

    return layout;
}

std::vector<UiVertex> BuildUiBackground(const UiLayout& layout)
{
    std::vector<UiVertex> vertices;

    vertices.reserve(600);
    const float s = layout.scale;
    const Color panelTop{0.47f, 0.53f, 0.54f};
    const Color panelBottom{0.32f, 0.39f, 0.42f};

    // Fixed dock backgrounds, followed by recessed areas for future content.
    Quad(vertices, layout.bottomBar, panelTop, panelBottom);
    Frame(vertices, layout.bottomBar, 2 * s, false);
    Quad(vertices, layout.toolbar, panelTop, panelBottom);
    Frame(vertices, layout.toolbar, 2 * s, false);
    Well(vertices, layout.toolbarContent, s);

    // News Ticker
    Solid(vertices, layout.newsTicker, {0.0f, 0.0f, 0.0f});
    Frame(vertices, layout.newsTicker, s, true);

    Well(vertices, layout.cityName, s);
    Well(vertices, layout.population, s);
    Well(vertices, layout.funds, s);
    Well(vertices, layout.zoneDemand, s);

    // Small R/C/I color keys only: these do not pretend to show demand values.
    const Color zoneColors[] = {{0.25f, 0.66f, 0.30f}, {0.25f, 0.51f, 0.83f}, {0.87f, 0.72f, 0.27f}};

    for (int i = 0; i < 3; ++i)
    {
        const float cell = (layout.zoneDemand.w - 16 * s) / 3;
        Solid(vertices, {layout.zoneDemand.x + 8 * s + i * cell, layout.zoneDemand.y + layout.zoneDemand.h - 8 * s,cell - 3 * s, 3 * s}, zoneColors[i]);
    }

    // The independently sized square is drawn last, over the dock junction.
    Quad(vertices, layout.minimap, panelTop, panelBottom);
    Frame(vertices, layout.minimap, 2 * s, false);
    Well(vertices, layout.minimapContent, s);

    return vertices;
}
