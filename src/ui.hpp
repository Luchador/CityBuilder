#pragma once

#include <cstdint>
#include <vector>
#include <functional>

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
    inline constexpr UiButtonId TransportationButton = 102;
    inline constexpr UiButtonId UtilitiesButton = 103;
    inline constexpr UiButtonId GovernmentButton = 104;
    inline constexpr UiButtonId BulldozerButton = 105;
    inline constexpr UiButtonId DisastersButton = 106;
    inline constexpr UiButtonId OptionsButton = 107;
    inline constexpr UiButtonId DataButton = 108;
    inline constexpr UiButtonId MeetButton = 109;
    inline constexpr UiButtonId DayButton = 110;

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

    inline constexpr UiButtonId RoadsMenuButton = 400;
    inline constexpr UiButtonId HighwaysMenuButton = 401;
    inline constexpr UiButtonId RailroadsMenuButton = 402;
    inline constexpr UiButtonId CommuterRailMenuButton = 403;
    inline constexpr UiButtonId SeaMenuButton = 404;
    inline constexpr UiButtonId AirportMenuButton = 405;

    inline constexpr UiButtonId DirtRoadButton = 410;
    inline constexpr UiButtonId RoadButton = 411;
    inline constexpr UiButtonId AvenueButton = 412;
    inline constexpr UiButtonId OneWayRoadButton = 413;
    inline constexpr UiButtonId TramButton = 414;
    inline constexpr UiButtonId TramStationButton = 415;
    // 420-429 reserved for future highway tools.
    inline constexpr UiButtonId RailroadButton = 430;
    inline constexpr UiButtonId RailroadPassengerStationButton = 431;
    inline constexpr UiButtonId RailroadFreightStationButton = 432;
    inline constexpr UiButtonId GrandStationButton = 433;

    inline constexpr UiButtonId SubwayButton = 440;
    inline constexpr UiButtonId SubwayStationButton = 441;
    inline constexpr UiButtonId ElevatedRailButton = 442;
    inline constexpr UiButtonId ElevatedRailStationButton = 443;
    inline constexpr UiButtonId SubwayElevatedConnectorButton = 444;
    inline constexpr UiButtonId MonorailButton = 445;
    inline constexpr UiButtonId MonorailStationButton = 446;

    inline constexpr UiButtonId FerryButton = 450;
    inline constexpr UiButtonId SeaportButton = 451;
    inline constexpr UiButtonId AirportButton = 460;
    inline constexpr UiButtonId RunwayButton = 461;
    inline constexpr UiButtonId TarmacButton = 462;
    inline constexpr UiButtonId TerminalButton = 463;
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
// Transportation definitions and texture assignments live in ui_transport.cpp.
void CreateTransportationButtons(UiButtons& buttons, TextureManager& textures);
void LayoutTransportationButtons(UiButtons& buttons, const UiLayout& layout);
void SetUiMinimized(UiSettings& settings, UiButtons& buttons, bool minimized);
void CancelUiButtonInteraction(UiButtons& buttons);

// Returns UI intent; the application handles camera actions and sounds.
UiButtonResult HandleUiButtonEvent(SDL_Window* window, const SDL_Event& event,
                                   const UiSettings& settings, UiButtons& buttons, UiMinimap& minimap);
void UpdateUiButtonPointer(SDL_Window* window, UiButtons& buttons, bool mousePanning);

struct UiButtonActions
{
    std::function<void()> zoomIn;
    std::function<void()> zoomOut;
    std::function<void(bool)> rotateCamera;
    std::function<void()> toggleUi;
};

void OnUiButtonClicked(UiButtonId id, const UiButtonActions& actions);
