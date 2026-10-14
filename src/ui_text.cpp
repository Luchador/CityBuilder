#include "ui_text.hpp"

#include <algorithm>

void BottomBarText::Initialize(TextRenderer& renderer)
{
    news = &renderer.CreateLabel("");
    const char* names[] = {"CITY", "POPULATION", "FUNDS", "DATE"};
    for (std::size_t i = 0; i < captions.size(); ++i)
    {
        captions[i] = &renderer.CreateLabel(names[i]);
        values[i] = &renderer.CreateLabel("");
    }
    SetValues(CityStatusText{});
}

void BottomBarText::SetValues(const CityStatusText& next)
{
    news->SetText(next.news);
    values[0]->SetText(next.cityName);
    values[1]->SetText(next.population);
    values[2]->SetText(next.funds);
    values[3]->SetText(next.date);
}

void BottomBarText::SetLayout(const UiLayout& layout)
{
    const float s = layout.scale;
    const float padding = 7.0f * s;
    TextStyle ticker;
    ticker.size = 15.0f * s;
    ticker.color = {0.92f, 0.91f, 0.75f, 1.0f};
    news->SetStyle(ticker);
    news->SetBounds({layout.newsTicker.x + padding, layout.newsTicker.y + s,
                     std::max(0.0f, layout.newsTicker.w - 2 * padding), layout.newsTicker.h - 2 * s});

    TextStyle caption;
    caption.size = 11.0f * s;
    caption.color = {0.65f, 0.76f, 0.79f, 1.0f};
    TextStyle value;
    value.fontAsset = "fonts/SourceSans3-Semibold.ttf";
    value.size = 17.0f * s;
    const UiRect panels[] = {layout.cityName, layout.population, layout.funds, layout.date};
    for (std::size_t i = 0; i < captions.size(); ++i)
    {
        const UiRect panel = panels[i];
        const float width = std::max(0.0f, panel.w - 2 * padding);
        // Accommodate shorter custom bars by dividing the available panel height.
        const float innerHeight = std::max(0.0f, panel.h - 4 * s);
        const float captionHeight = std::min(14.0f * s, innerHeight * 0.4f);
        captions[i]->SetStyle(caption);
        captions[i]->SetBounds({panel.x + padding, panel.y + 2 * s, width, captionHeight});
        values[i]->SetStyle(value);
        values[i]->SetBounds({panel.x + padding, panel.y + 2 * s + captionHeight,
                              width, innerHeight - captionHeight});
    }
}
