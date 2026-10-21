#include "ui_tool_buttons.hpp"
#include <array>

namespace
{
    // Nearest the toolbar first, extending left, like the existing zoning menu.
    constexpr std::array<UiToolButtonDefinition, 6> Categories{{
        {UiButtonIds::RoadsMenuButton, "Streets", "ui/toolbar/btn_streets"},
        {UiButtonIds::HighwaysMenuButton, "Highways", "ui/toolbar/btn_hwy"},
        {UiButtonIds::RailroadsMenuButton, "Railroads", "ui/toolbar/btn_rail"},
        {UiButtonIds::CommuterRailMenuButton, "Commuter Rail", "ui/toolbar/btn_commuter_rail"},
        {UiButtonIds::SeaMenuButton, "Sea", "ui/toolbar/btn_sea"},
        {UiButtonIds::AirportMenuButton, "Airport", "ui/toolbar/btn_air"}
    }};

    // Submenu order is top to bottom, directly below the category button.
    constexpr std::array<UiToolButtonDefinition, 6> Roads{{
        {UiButtonIds::DirtRoadButton, "Dirt Road", "ui/toolbar/btn_dirt_road"},
        {UiButtonIds::RoadButton, "Road", "ui/toolbar/btn_road"},
        {UiButtonIds::AvenueButton, "Avenue", "ui/toolbar/btn_avenue"},
        {UiButtonIds::OneWayRoadButton, "One-way", "ui/toolbar/btn_oneway"},
        {UiButtonIds::TramButton, "Tram", "ui/toolbar/btn_tram"},
        {UiButtonIds::TramStationButton, "Tram Stop", "ui/toolbar/btn_tram_stop"}
    }};

    constexpr std::array<UiToolButtonDefinition, 5> Highways{{
        {UiButtonIds::HighwayButton, "Highway", "ui/toolbar/btn_hwy"},
        {UiButtonIds::ThreeWayInterchangeButton, "3-Way Interchange", "ui/toolbar/btn_3way"},
        {UiButtonIds::CloverLeafInterchangeButton, "Clover Leaf Interchange", "ui/toolbar/btn_clover"},
        {UiButtonIds::OverpassButton, "Overpass", "ui/toolbar/btn_overpass"},
        {UiButtonIds::OnRampButton, "On-ramp", "ui/toolbar/btn_onramp"}
    }};

    constexpr std::array<UiToolButtonDefinition, 4> Railroads{{
        {UiButtonIds::RailroadButton, "Railroad", "ui/toolbar/btn_railroad"},
        {UiButtonIds::RailroadPassengerStationButton, "Passenger Station", "ui/toolbar/btn_passenger_station"},
        {UiButtonIds::RailroadFreightStationButton, "Freight Station", "ui/toolbar/btn_freight_station"},
        {UiButtonIds::GrandStationButton, "Grand Station", "ui/toolbar/btn_grand_station"}
    }};

    constexpr std::array<UiToolButtonDefinition, 7> CommuterRail{{
        {UiButtonIds::SubwayButton, "Subway", "ui/toolbar/btn_subway"},
        {UiButtonIds::SubwayStationButton, "Subway Station", "ui/toolbar/btn_subway_station"},
        {UiButtonIds::ElevatedRailButton, "Elevated Rail", "ui/toolbar/btn_elevated_rail"},
        {UiButtonIds::ElevatedRailStationButton, "Elevated Rail Station", "ui/toolbar/btn_elevated_rail_station"},
        {UiButtonIds::SubwayElevatedConnectorButton, "Subway to Elevated Rail Connector", "ui/toolbar/btn_s_to_e"},
        {UiButtonIds::MonorailButton, "Monorail", "ui/toolbar/btn_monorail"},
        {UiButtonIds::MonorailStationButton, "Monorail Station", "ui/toolbar/btn_monorail_station"}
    }};

    constexpr std::array<UiToolButtonDefinition, 2> Sea{{
        {UiButtonIds::FerryButton, "Ferry", "ui/toolbar/btn_ferry"},
        {UiButtonIds::SeaportButton, "Seaport", "ui/toolbar/btn_seaport"}
    }};

    constexpr std::array<UiToolButtonDefinition, 4> Airport{{
        {UiButtonIds::AirportButton, "Airport", "ui/toolbar/btn_airport"},
        {UiButtonIds::RunwayButton, "Runway", "ui/toolbar/btn_runway"},
        {UiButtonIds::TarmacButton, "Tarmac", "ui/toolbar/btn_tarmac"},
        {UiButtonIds::TerminalButton, "Terminal", "ui/toolbar/btn_terminal"}
    }};

    constexpr std::array<UiToolMenuDefinition, 6> Menus{{
        {UiButtonIds::RoadsMenuButton, Roads},
        {UiButtonIds::HighwaysMenuButton, Highways},
        {UiButtonIds::RailroadsMenuButton, Railroads},
        {UiButtonIds::CommuterRailMenuButton, CommuterRail},
        {UiButtonIds::SeaMenuButton, Sea},
        {UiButtonIds::AirportMenuButton, Airport}
    }};
}

void CreateTransportationButtons(UiButtons& buttons, TextureManager& textures)
{
    AddToolFlyout(buttons, textures, UiButtonIds::TransportationButton, Categories, UiButtonShape::Circle);
    for (const auto& menu : Menus)
        AddToolFlyout(buttons, textures, menu.trigger, menu.buttons, UiButtonShape::Rectangle);
}

void LayoutTransportationButtons(UiButtons& buttons, const UiLayout& layout)
{
    LayoutToolFlyouts(buttons, layout, UiButtonIds::TransportationButton, Categories, Menus);
}
