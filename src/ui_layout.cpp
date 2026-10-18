#include "ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>

bool UiRect::Contains(float px, float py) const
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

/**
 * Remove the upper left corner of the minimap panel with a 45 degree cut.
 */
float UiLayout::GetMinimapCornerCut() const
{
    const float padding = std::max(0.0f, minimapContent.x - minimapPanel.x);

    return std::max(0.0f, std::min(minimapPanel.w, minimapPanel.h) * 0.5f - (std::sqrt(2.0f) - 1.0f) * padding);
}

bool UiLayout::IsMinimapPanelPoint(float x, float y) const
{
    return !minimized && minimapPanel.Contains(x, y) && (x - minimapPanel.x) + (y - minimapPanel.y) >= GetMinimapCornerCut();
}

bool UiLayout::IsWorldPoint(float x, float y) const
{
    return world.Contains(x, y) && !IsMinimapPanelPoint(x, y);
}

namespace
{
    UiRect Inset(UiRect r, float amount)
    {
        return {r.x + amount, r.y + amount, std::max(0.0f, r.w - 2 * amount), std::max(0.0f, r.h - 2 * amount)};
    }

    struct Color { float r, g, b; };
    struct Point { float x, y; };

    std::array<Point, 5> CutCornerPoints(UiRect r, float cut)
    {
        // Clockwise in screen coordinates, starting at the shortened top edge.
        return {{{r.x + cut, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}, {r.x, r.y + cut}}};
    }

    void MinimapPanel(std::vector<UiVertex>& vertices, const UiLayout& layout, Color top, Color bottom)
    {
        const UiRect r = layout.minimapPanel;

        if (r.w <= 0.0f || r.h <= 0.0f)
        {
            return;
        }

        const float cut = layout.GetMinimapCornerCut();
        const auto outer = CutCornerPoints(r, cut);

        const auto gradientVertex = [=](Point p)
        {
            const float t = (p.y - r.y) / r.h;
            return UiVertex{p.x, p.y,
                top.r + (bottom.r - top.r) * t,
                top.g + (bottom.g - top.g) * t,
                top.b + (bottom.b - top.b) * t, 1.0f};
        };

        // A convex pentagon needs three triangles. Its removed corner has
        // no geometry, allowing the already-rendered world to show through.
        for (std::size_t i = 1; i + 1 < outer.size(); ++i)
        {
            vertices.insert(vertices.end(), {gradientVertex(outer[0]), gradientVertex(outer[i]), gradientVertex(outer[i + 1])});
        }

        const float edge = std::min(2.0f * layout.scale, std::min(r.w, r.h) * 0.125f);

        if (edge <= 0.0f)
        {
            return;
        }

        // Offset every edge inward by the same perpendicular distance.
        // The diagonal's x+y intercept moves by sqrt(2)*edge; subtract 2*edge
        // because Inset() also moves the new rectangle's origin on both axes.
        const float innerCut = std::max(0.0f, cut + (std::sqrt(2.0f) - 2.0f) * edge);
        const auto inner = CutCornerPoints(Inset(r, edge), innerCut);

        const Color light{0.72f, 0.75f, 0.74f};
        const Color shadow{0.17f, 0.22f, 0.24f};

        // Five joined strips form a continuous frame, with no overlaps or
        // gaps at the two new corners. Top/left/diagonal catch the light.
        for (std::size_t i = 0; i < outer.size(); ++i)
        {
            const std::size_t next = (i + 1) % outer.size();
            const Color color = (i == 1 || i == 2) ? shadow : light;
            const auto vertex = [=](Point p) { return UiVertex{p.x, p.y, color.r, color.g, color.b, 1.0f}; };
            const UiVertex a = vertex(outer[i]), b = vertex(outer[next]);
            const UiVertex c = vertex(inner[next]), d = vertex(inner[i]);

            vertices.insert(vertices.end(), {a, b, c, a, c, d});
        }
    }

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
    layout.minimized = settings.minimized;

    // Normally 1. Window minimum size is 800x480; retain safe geometry even
    // if a platform temporarily reports a smaller non-minimized window.
    const float s = std::min({1.0f, layout.width / 800.0f, layout.height / 480.0f});
    layout.scale = s;
    const float bottom = std::clamp(settings.barSlimDimension, 72.0f, 128.0f) * s;

    // Make the toolbar width the same as the bottom bar height.
    const float right = bottom;

    // Reserve useful space for city information and toolbar controls.
    const float maxMap = std::min({512.0f * s, layout.width - 480.0f * s, layout.height - 220.0f * s});
    const float map = std::clamp(settings.minimapSize * s, 128.0f * s, maxMap);

    layout.world = settings.minimized ? UiRect{0, 0, layout.width, layout.height} : UiRect{0, 0, layout.width - right, layout.height - bottom};
    layout.bottomBar = {0, layout.height - bottom, layout.width, bottom};
    layout.toolbar = {layout.width - right, 0, right, layout.height - bottom};
    layout.minimapPanel = {layout.width - map, layout.height - map, map, map};

    const float gap = 8.0f * s;
    // Both the bar and toolbar stop their content before the minimap frame.
    const float contentWidth = std::min(layout.minimapPanel.x, layout.toolbar.x) - 2 * gap;
    const float demandW = 80.0f * s;

    // Preserve the wide RCI gap at the normal window size, but give the four
    // information panels priority when the window narrows or the minimap grows.
    const float demandGap = std::clamp(contentWidth - demandW - 640.0f * s, gap, 200.0f * s);

    const float tickerWidth = contentWidth - demandW - demandGap;

    layout.newsTicker = {gap, layout.bottomBar.y + gap, tickerWidth, 28.0f * s};

    const float rowY = layout.newsTicker.y + layout.newsTicker.h + gap;
    const float rowH = layout.height - gap - rowY;
    const float statsW = tickerWidth - 3.0f * gap;

    layout.cityName = {gap, rowY, statsW * 0.32f, rowH};
    layout.population = {layout.cityName.x + layout.cityName.w + gap, rowY, statsW * 0.22f, rowH};
    layout.funds = {layout.population.x + layout.population.w + gap, rowY, statsW * 0.24f, rowH};
    layout.date = {layout.funds.x + layout.funds.w + gap, rowY, statsW * 0.22f, rowH};
    layout.zoneDemand = {gap + contentWidth - demandW, layout.bottomBar.y + gap, demandW, layout.bottomBar.h - 2.0f * gap};
    // The 84-pixel toolbar art fits the normal 92-pixel bar with 4-pixel margins.
    const float toolbarPadding = 4.0f * s;
    layout.toolbarContent = {layout.toolbar.x + toolbarPadding, toolbarPadding,
        right - 2 * toolbarPadding, std::min(layout.minimapPanel.y, layout.world.h) - 2 * toolbarPadding};
    layout.minimapContent = Inset(layout.minimapPanel, gap);

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

    // Toolbar.
    Quad(vertices, layout.toolbar, panelTop, panelTop);
    Frame(vertices, layout.toolbar, 2 * s, false);

    // News Ticker.
    Solid(vertices, layout.newsTicker, {0.0f, 0.0f, 0.0f});
    Frame(vertices, layout.newsTicker, s, true);

    Well(vertices, layout.cityName, s);
    Well(vertices, layout.population, s);
    Well(vertices, layout.funds, s);
    Well(vertices, layout.date, s);
    Well(vertices, layout.zoneDemand, s);

    // RCI indicator.
    const Color zoneColors[] = {{0.25f, 0.66f, 0.30f}, {0.25f, 0.51f, 0.83f}, {0.87f, 0.72f, 0.27f}};

    for (int i = 0; i < 3; ++i)
    {
        const float cell = (layout.zoneDemand.w - 16 * s) / 3;
        Solid(vertices, {layout.zoneDemand.x + 8 * s + i * cell, layout.zoneDemand.y + layout.zoneDemand.h - 8 * s,cell - 3 * s, 3 * s}, zoneColors[i]);
    }

    // Minimap background and frame share the same five-sided outline.
    MinimapPanel(vertices, layout, panelTop, panelBottom);

    return vertices;
}
