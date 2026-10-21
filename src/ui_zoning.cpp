#include "ui_tool_buttons.hpp"
#include <array>

namespace
{
    // Nearest the toolbar first; submenu tools descend below their category.
    constexpr std::array<UiToolButtonDefinition, 4> Categories{{
        {UiButtonIds::ResidentialButton, "Residential", "ui/toolbar/btn_res"},
        {UiButtonIds::CommercialButton, "Commercial", "ui/toolbar/btn_com"},
        {UiButtonIds::IndustrialButton, "Industrial", "ui/toolbar/btn_ind"},
        {UiButtonIds::DezoneButton, "Dezone", "ui/toolbar/btn_dezone"}
    }};
    constexpr std::array<UiToolButtonDefinition, 3> Residential{{
        {UiButtonIds::LightResidentialButton, "Light Residential", "ui/toolbar/btn_res_light"},
        {UiButtonIds::MediumResidentialButton, "Medium Residential", "ui/toolbar/btn_res_medium"},
        {UiButtonIds::DenseResidentialButton, "Dense Residential", "ui/toolbar/btn_res_dense"}
    }};
    constexpr std::array<UiToolButtonDefinition, 3> Commercial{{
        {UiButtonIds::LightCommercialButton, "Light Commercial", "ui/toolbar/btn_com_light"},
        {UiButtonIds::MediumCommercialButton, "Medium Commercial", "ui/toolbar/btn_com_medium"},
        {UiButtonIds::DenseCommercialButton, "Dense Commercial", "ui/toolbar/btn_com_dense"}
    }};
    constexpr std::array<UiToolButtonDefinition, 3> Industrial{{
        {UiButtonIds::FarmButton, "Farm", "ui/toolbar/btn_farm"},
        {UiButtonIds::MediumIndustrialButton, "Medium Industrial", "ui/toolbar/btn_ind_medium"},
        {UiButtonIds::HeavyIndustrialButton, "Heavy Industrial", "ui/toolbar/btn_ind_heavy"}
    }};
    constexpr std::array<UiToolMenuDefinition, 3> Menus{{
        {UiButtonIds::ResidentialButton, Residential},
        {UiButtonIds::CommercialButton, Commercial},
        {UiButtonIds::IndustrialButton, Industrial}
    }};
}

void CreateZoningButtons(UiButtons& buttons, TextureManager& textures)
{
    AddToolFlyout(buttons, textures, UiButtonIds::ZonesButton, Categories, UiButtonShape::Circle);
    for (const auto& menu : Menus)
        AddToolFlyout(buttons, textures, menu.trigger, menu.buttons, UiButtonShape::Rectangle);
}

void LayoutZoningButtons(UiButtons& buttons, const UiLayout& layout)
{
    LayoutToolFlyouts(buttons, layout, UiButtonIds::ZonesButton, Categories, Menus);
}
