#include "ui_tool_buttons.hpp"
#include <array>

namespace
{
    // Nearest the toolbar first, extending left. Subtools descend below each category.
    constexpr std::array<UiToolButtonDefinition, 4> Categories{{
        {UiButtonIds::WaterMenuButton, "Water", "ui/toolbar/btn_water"},
        {UiButtonIds::PowerMenuButton, "Power", "ui/toolbar/btn_power"},
        {UiButtonIds::LightsMenuButton, "Lights", "ui/toolbar/btn_lights"},
        {UiButtonIds::GarbageMenuButton, "Garbage", "ui/toolbar/btn_garbage"}
    }};

    constexpr std::array<UiToolButtonDefinition, 5> Water{{
        {UiButtonIds::WaterPipeButton, "Water Pipe", "ui/toolbar/btn_water_pipe"},
        {UiButtonIds::WaterTowerButton, "Water Tower", "ui/toolbar/btn_water_tower"},
        {UiButtonIds::WaterPumpButton, "Water Pump", "ui/toolbar/btn_pump"},
        {UiButtonIds::DesalinationPlantButton, "Desalination Plant", "ui/toolbar/btn_desalination_plant"},
        {UiButtonIds::WaterTreatmentPlantButton, "Water Treatment Plant", "ui/toolbar/btn_water_treatment"}
    }};

    constexpr std::array<UiToolButtonDefinition, 10> Power{{
        {UiButtonIds::PowerLineButton, "Power Line", "ui/toolbar/btn_power_line"},
        {UiButtonIds::UndergroundLineButton, "Underground Line", "ui/toolbar/btn_underground_line"},
        {UiButtonIds::CoalPowerPlantButton, "Coal Power Plant", "ui/toolbar/btn_coal"},
        {UiButtonIds::OilPowerPlantButton, "Oil Power Plant", "ui/toolbar/btn_oil"},
        {UiButtonIds::GasPowerPlantButton, "Gas Power Plant", "ui/toolbar/btn_gas"},
        {UiButtonIds::WindTurbineButton, "Wind Turbine", "ui/toolbar/btn_wind_turbine"},
        {UiButtonIds::SolarPowerPlantButton, "Solar Power Plant", "ui/toolbar/btn_solar"},
        {UiButtonIds::NuclearPowerPlantButton, "Nuclear Power Plant", "ui/toolbar/btn_nuclear"},
        {UiButtonIds::MicrowavePowerPlantButton, "Microwave Power Plant", "ui/toolbar/btn_microwave"},
        {UiButtonIds::FusionPowerPlantButton, "Fusion Power Plant", "ui/toolbar/btn_fusion"}
    }};

    constexpr std::array<UiToolButtonDefinition, 4> Lights{{
        {UiButtonIds::WoodPoleButton, "Wood Pole", "ui/toolbar/btn_wood_pole"},
        {UiButtonIds::MetalPoleButton, "Metal Pole", "ui/toolbar/btn_metal_pole"},
        {UiButtonIds::StringLightsButton, "String Lights", "ui/toolbar/btn_string_lights"},
        {UiButtonIds::SpotlightButton, "Spotlight", "ui/toolbar/btn_spotlight"}
    }};

    constexpr std::array<UiToolButtonDefinition, 4> Garbage{{
        {UiButtonIds::LandfillButton, "Landfill", "ui/toolbar/btn_landfill"},
        {UiButtonIds::IncineratorButton, "Incinerator", "ui/toolbar/btn_incinerator"},
        {UiButtonIds::RecyclingCenterButton, "Recycling Center", "ui/toolbar/btn_recycling_center"},
        {UiButtonIds::WasteToEnergyPlantButton, "Waste to Energy Plant", "ui/toolbar/btn_waste_to_energy"}
    }};

    constexpr std::array<UiToolMenuDefinition, 4> Menus{{
        {UiButtonIds::WaterMenuButton, Water},
        {UiButtonIds::PowerMenuButton, Power},
        {UiButtonIds::LightsMenuButton, Lights},
        {UiButtonIds::GarbageMenuButton, Garbage}
    }};
}

void CreateUtilityButtons(UiButtons& buttons, TextureManager& textures)
{
    AddToolFlyout(buttons, textures, UiButtonIds::UtilitiesButton, Categories, UiButtonShape::Circle);
    for (const auto& menu : Menus)
        AddToolFlyout(buttons, textures, menu.trigger, menu.buttons, UiButtonShape::Rectangle);
}

void LayoutUtilityButtons(UiButtons& buttons, const UiLayout& layout)
{
    LayoutToolFlyouts(buttons, layout, UiButtonIds::UtilitiesButton, Categories, Menus);
}
