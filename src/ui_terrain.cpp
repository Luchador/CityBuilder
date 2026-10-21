#include "ui_tool_buttons.hpp"
#include <array>

namespace
{
    // Nearest the toolbar first, extending left. The first four are direct tools.
    constexpr std::array<UiToolButtonDefinition, 6> Categories{{
        {UiButtonIds::RaiseTerrainButton, "Raise Terrain", "ui/toolbar/btn_raise_terrain"},
        {UiButtonIds::LowerTerrainButton, "Lower Terrain", "ui/toolbar/btn_lower_terrain"},
        {UiButtonIds::FlattenTerrainButton, "Flatten Terrain", "ui/toolbar/btn_flatten_terrain"},
        {UiButtonIds::CreateWaterButton, "Create Water", "ui/toolbar/btn_create_water"},
        {UiButtonIds::PlantFoliageMenuButton, "Plant Foliage", "ui/toolbar/btn_plant_trees"},
        {UiButtonIds::CreateAnimalMenuButton, "Create Animal", "ui/toolbar/btn_create_animal"}
    }};

    // Top to bottom.
    constexpr std::array<UiToolButtonDefinition, 7> Foliage{{
        {UiButtonIds::DeciduousButton, "Deciduous", "ui/toolbar/btn_deciduous"},
        {UiButtonIds::EvergreenButton, "Evergreen", "ui/toolbar/btn_evergreen"},
        {UiButtonIds::RedwoodButton, "Redwood", "ui/toolbar/btn_redwood"},
        {UiButtonIds::PrairieButton, "Prairie", "ui/toolbar/btn_prairie"},
        {UiButtonIds::ChaparralButton, "Chaparral", "ui/toolbar/btn_chaparral"},
        {UiButtonIds::XerophytesButton, "Xerophytes", "ui/toolbar/btn_xerophyte"},
        {UiButtonIds::SubtropicalButton, "Subtropical", "ui/toolbar/btn_subtropical"}
    }};

    // Left to right, then down: three columns and six rows.
    constexpr std::array<UiToolButtonDefinition, 18> Animals{{
        {UiButtonIds::BisonButton, "Bison", "ui/toolbar/btn_bison"},
        {UiButtonIds::MooseButton, "Moose", "ui/toolbar/btn_moose"},
        {UiButtonIds::DeerButton, "Deer", "ui/toolbar/btn_deer"},
        {UiButtonIds::BearButton, "Bear", "ui/toolbar/btn_bear"},
        {UiButtonIds::WolfButton, "Wolf", "ui/toolbar/btn_wolf"},
        {UiButtonIds::JavelinaButton, "Javelina", "ui/toolbar/btn_javelina"},
        {UiButtonIds::BighornSheepButton, "Bighorn Sheep", "ui/toolbar/btn_bighorn_sheep"},
        {UiButtonIds::FoxButton, "Fox", "ui/toolbar/btn_fox"},
        {UiButtonIds::CougarButton, "Cougar", "ui/toolbar/btn_cougar"},
        {UiButtonIds::BobcatButton, "Bobcat", "ui/toolbar/btn_bobcat"},
        {UiButtonIds::BeaverButton, "Beaver", "ui/toolbar/btn_beaver"},
        {UiButtonIds::DolphinButton, "Dolphin", "ui/toolbar/btn_dolphin"},
        {UiButtonIds::SharkButton, "Shark", "ui/toolbar/btn_shark"},
        {UiButtonIds::AlligatorButton, "Alligator", "ui/toolbar/btn_alligator"},
        {UiButtonIds::RoadrunnerButton, "Roadrunner", "ui/toolbar/btn_roadrunner"},
        {UiButtonIds::RavenButton, "Raven", "ui/toolbar/btn_raven"},
        {UiButtonIds::CaliforniaCondorButton, "California Condor", "ui/toolbar/btn_calif_condor"},
        {UiButtonIds::BaldEagleButton, "Bald Eagle", "ui/toolbar/btn_bald_eagle"}
    }};

    constexpr std::array<UiToolMenuDefinition, 2> Menus{{
        {UiButtonIds::PlantFoliageMenuButton, Foliage},
        {UiButtonIds::CreateAnimalMenuButton, Animals, 3}
    }};
}

void CreateTerrainButtons(UiButtons& buttons, TextureManager& textures)
{
    AddToolFlyout(buttons, textures, UiButtonIds::TerrainButton, Categories, UiButtonShape::Circle);
    for (const auto& menu : Menus)
        AddToolFlyout(buttons, textures, menu.trigger, menu.buttons, UiButtonShape::Rectangle);
}

void LayoutTerrainButtons(UiButtons& buttons, const UiLayout& layout)
{
    LayoutToolFlyouts(buttons, layout, UiButtonIds::TerrainButton, Categories, Menus);
}
