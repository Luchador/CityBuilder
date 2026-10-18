#pragma once

#include <cstdint>
#include <vector>

class TextureManager;
class UiButtons;
class UiMinimap;
struct UiButtonResult;
struct SDL_Window;
union SDL_Event;

using UiButtonId = std::uint32_t;

namespace UiButtonIds
{
    inline constexpr UiButtonId ZoomInButton = 1;
    inline constexpr UiButtonId ZoomOutButton = 2;
    inline constexpr UiButtonId RotateClockwiseButton = 3;
    inline constexpr UiButtonId RotateCounterClockwiseButton = 4;
    inline constexpr UiButtonId MinimizeUIButton = 5;

    inline constexpr UiButtonId TerrainButton = 100;
    inline constexpr UiButtonId ZonesButton = 101;
    inline constexpr UiButtonId InfrastructureButton = 102;
    inline constexpr UiButtonId UtilitiesButton = 103;
    inline constexpr UiButtonId GovernmentButton = 104;
    inline constexpr UiButtonId DisastersButton = 105;

    inline constexpr UiButtonId ResidentialButton = 200;
    inline constexpr UiButtonId CommercialButton = 201;
    inline constexpr UiButtonId IndustrialButton = 202;
    inline constexpr UiButtonId DezoneButton = 203;

    inline constexpr UiButtonId TickerPauseButton = 300;
    inline constexpr UiButtonId TickerSpeedButton1 = 301;
    inline constexpr UiButtonId TickerSpeedButton2 = 302;
    inline constexpr UiButtonId TickerSpeedButton3 = 303;
    inline constexpr UiButtonId TickerSpeedButton4 = 304;
    inline constexpr UiButtonId TickerPlayButton = 305;
}

struct UiRect
{
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const;
};

struct UiSettings
{
    // Height of the bottom bar, or width of the toolbar.
    float barSlimDimension = 92.0f;

    float minimapSize = 288.0f;

    bool minimized = false;
};

struct UiLayout
{
    float width = 0, height = 0, scale = 1;
    UiRect world, bottomBar, toolbar, minimapPanel;
    UiRect newsTicker, speedTicker, cityName, population, funds, date, zoneDemand;
    UiRect toolbarContent, minimapContent;

    // Keep panel bounds for the restore button, but give the world the full window.
    bool minimized = false;

    // minimapPanel remains the bounding rectangle. These describe its
    // actual five-sided outline, including the upper-left corner cut.
    float GetMinimapCornerCut() const;

    bool IsMinimapPanelPoint(float x, float y) const;
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

// Application-level UI composition. TextureManager must outlive the buttons.
void CreateUiButtons(UiButtons& buttons, TextureManager& textures);
void LayoutUiButtons(UiButtons& buttons, UiMinimap& minimap, const UiLayout& layout);
void SetUiMinimized(UiSettings& settings, UiButtons& buttons, bool minimized);
void CancelUiButtonInteraction(UiButtons& buttons);

// Returns UI intent; the application handles camera actions and sounds.
UiButtonResult HandleUiButtonEvent(SDL_Window* window, const SDL_Event& event,
                                   const UiSettings& settings, UiButtons& buttons, UiMinimap& minimap);
void UpdateUiButtonPointer(SDL_Window* window, UiButtons& buttons, bool mousePanning);
