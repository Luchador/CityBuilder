#include "ui.hpp"
#include "ui_buttons.hpp"
#include "texture.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>

namespace
{
    struct TransportButtonDefinition
    {
        UiButtonId id;
        const char* tooltip;
        // Set this to a path relative to assets/, without the state suffix or
        // .png. For example "ui/toolbar/btn_road" loads btn_road_normal.png,
        // btn_road_hov.png and btn_road_pressed.png. Empty uses the placeholder.
        const char* imageStem = "";
        UiButtonShape shape = UiButtonShape::Rectangle;
        const char* hoverSuffix = "_hov.png";
    };

    // Reuse an existing image until the new textures are assigned below.
    constexpr const char* PlaceholderStem = "ui/toolbar/btn_transp";
    constexpr float ButtonWidth = 84.0f;
    constexpr float ButtonHeight = 50.0f;
    constexpr float ButtonGap = 8.0f;

    // Nearest the toolbar first, extending left, like the existing zoning menu.
    constexpr std::array<TransportButtonDefinition, 6> Categories{{
        {UiButtonIds::RoadsMenuButton, "Roads", "ui/toolbar/btn_road"},
        {UiButtonIds::HighwaysMenuButton, "Highways", "ui/toolbar/btn_hwy"},
        {UiButtonIds::RailroadsMenuButton, "Railroads", "ui/toolbar/btn_rail"},
        {UiButtonIds::CommuterRailMenuButton, "Commuter Rail", "ui/toolbar/btn_commuter_rail"},
        {UiButtonIds::SeaMenuButton, "Sea", "ui/toolbar/btn_sea"},
        {UiButtonIds::AirportMenuButton, "Air", "ui/toolbar/btn_air"}
    }};

    // Submenu order is top to bottom, directly below the category button.
    constexpr std::array<TransportButtonDefinition, 6> Roads{{
        {UiButtonIds::DirtRoadButton, "Dirt Road", "ui/toolbar/btn_dirt_road"},
        {UiButtonIds::RoadButton, "Road", "ui/toolbar/btn_road"},
        {UiButtonIds::AvenueButton, "Avenue", "ui/toolbar/btn_avenue"},
        {UiButtonIds::OneWayRoadButton, "One-way", "ui/toolbar/btn_oneway"},
        {UiButtonIds::TramButton, "Tram", "ui/toolbar/btn_tram"},
        {UiButtonIds::TramStationButton, "Tram Stop", "ui/toolbar/btn_tram_stop"}
    }};

    constexpr std::array<TransportButtonDefinition, 0> Highways{}; // Add tools here later.

    constexpr std::array<TransportButtonDefinition, 4> Railroads{{
        {UiButtonIds::RailroadButton, "Railroad", "ui/toolbar/btn_railroad"},
        {UiButtonIds::RailroadPassengerStationButton, "Passenger Station", "ui/toolbar/btn_passenger_station"},
        {UiButtonIds::RailroadFreightStationButton, "Freight Station", "ui/toolbar/btn_freight_station"},
        {UiButtonIds::GrandStationButton, "Grand Station", "ui/toolbar/btn_grand_station"}
    }};

    constexpr std::array<TransportButtonDefinition, 7> CommuterRail{{
        {UiButtonIds::SubwayButton, "Subway", "ui/toolbar/btn_subway"},
        {UiButtonIds::SubwayStationButton, "Subway Station", "ui/toolbar/btn_subway_station"},
        {UiButtonIds::ElevatedRailButton, "Elevated Rail", "ui/toolbar/btn_elevated_rail"},
        {UiButtonIds::ElevatedRailStationButton, "Elevated Rail Station", "ui/toolbar/btn_elevated_rail_station"},
        {UiButtonIds::SubwayElevatedConnectorButton, "Subway to Elevated Rail Connector", "ui/toolbar/btn_s_to_e"},
        {UiButtonIds::MonorailButton, "Monorail", "ui/toolbar/btn_monorail"},
        {UiButtonIds::MonorailStationButton, "Monorail Station", "ui/toolbar/btn_monorail_station"}
    }};

    constexpr std::array<TransportButtonDefinition, 2> Sea{{
        {UiButtonIds::FerryButton, "Ferry", "ui/toolbar/btn_ferry"},
        {UiButtonIds::SeaportButton, "Seaport", "ui/toolbar/btn_seaport"}
    }};

    constexpr std::array<TransportButtonDefinition, 4> Airport{{
        {UiButtonIds::AirportButton, "Airport", "ui/toolbar/btn_airport"},
        {UiButtonIds::RunwayButton, "Runway", "ui/toolbar/btn_runway"},
        {UiButtonIds::TarmacButton, "Tarmac", "ui/toolbar/btn_tarmac"},
        {UiButtonIds::TerminalButton, "Terminal", "ui/toolbar/btn_terminal"}
    }};

    struct TransportMenuDefinition
    {
        UiButtonId trigger;
        std::span<const TransportButtonDefinition> buttons;
    };
    constexpr std::array<TransportMenuDefinition, 6> Menus{{
        {UiButtonIds::RoadsMenuButton, Roads},
        {UiButtonIds::HighwaysMenuButton, Highways},
        {UiButtonIds::RailroadsMenuButton, Railroads},
        {UiButtonIds::CommuterRailMenuButton, CommuterRail},
        {UiButtonIds::SeaMenuButton, Sea},
        {UiButtonIds::AirportMenuButton, Airport}
    }};

    void AddMenu(UiButtons& buttons, TextureManager& textures, UiButtonId trigger,
                 std::span<const TransportButtonDefinition> definitions)
    {
        if (definitions.empty()) return;
        std::vector<UiButtonId> children;
        for (const auto& definition : definitions)
        {
            const std::string stem = *definition.imageStem ? definition.imageStem : PlaceholderStem;
            auto& button = buttons.Add(definition.id, {
                &textures.Load(stem + "_normal.png"),
                &textures.Load(stem + definition.hoverSuffix),
                &textures.Load(stem + "_pressed.png")
            });
            button.SetTooltip(definition.tooltip);
            button.SetShape(definition.shape);
            children.push_back(definition.id);
        }
        buttons.AddFlyout(trigger, children);
    }
}

void CreateTransportationButtons(UiButtons& buttons, TextureManager& textures)
{
    AddMenu(buttons, textures, UiButtonIds::TransportationButton, Categories);
    for (const auto& menu : Menus) AddMenu(buttons, textures, menu.trigger, menu.buttons);
}

void LayoutTransportationButtons(UiButtons& buttons, const UiLayout& layout)
{
    const auto* trigger = buttons.Find(UiButtonIds::TransportationButton);
    if (!trigger) return;
    const auto anchor = trigger->GetBounds();
    const float margin = 4.0f * layout.scale;
    const float attachmentX = std::min(anchor.x, layout.toolbar.x);
    const float categoryY = anchor.y;
    const float bottom = std::min(layout.height - margin, layout.bottomBar.y - margin);
    std::size_t longestMenu = 0;
    for (const auto& menu : Menus) longestMenu = std::max(longestMenu, menu.buttons.size());

    // Fit both the horizontal row and the longest descending submenu. Keep all
    // categories the same size, including when a short window shrinks the toolbar.
    const float rowWidth = static_cast<float>(Categories.size()) * (ButtonWidth + ButtonGap);
    const float columnHeight = ButtonHeight + static_cast<float>(longestMenu) * (ButtonHeight + ButtonGap);
    const float scale = std::max(0.0f, std::min({layout.scale, anchor.w / ButtonWidth,
        anchor.h / ButtonHeight, (attachmentX - margin) / rowWidth, (bottom - categoryY) / columnHeight}));
    const float width = ButtonWidth * scale, height = ButtonHeight * scale, gap = ButtonGap * scale;
    float x = attachmentX - gap - width;
    float left = anchor.x;
    for (const auto& category : Categories)
    {
        buttons.Find(category.id)->SetBounds({x, categoryY, width, height});
        left = x;
        x -= width + gap;
    }
    buttons.SetFlyoutBounds(UiButtonIds::TransportationButton,
        {left, categoryY, anchor.x + anchor.w - left, std::max(height, anchor.h)});

    for (const auto& menu : Menus)
    {
        if (menu.buttons.empty()) continue;
        const auto parent = buttons.Find(menu.trigger)->GetBounds();
        float y = parent.y + parent.h + gap;
        for (const auto& child : menu.buttons)
        {
            buttons.Find(child.id)->SetBounds({parent.x, y, width, height});
            y += height + gap;
        }
        // Include the gaps so moving from the category into its column is safe.
        buttons.SetFlyoutBounds(menu.trigger, {parent.x, parent.y, width, y - gap - parent.y});
    }
}
