#include "ui_tool_buttons.hpp"
#include <array>

namespace
{
    // Nearest the toolbar first, extending left.
    constexpr std::array<UiToolButtonDefinition, 7> Categories{{
        {UiButtonIds::PoliceMenuButton, "Police", "ui/toolbar/btn_police"},
        {UiButtonIds::FireMenuButton, "Fire", "ui/toolbar/btn_fire"},
        {UiButtonIds::HealthMenuButton, "Health", "ui/toolbar/btn_health"},
        {UiButtonIds::EducationMenuButton, "Education", "ui/toolbar/btn_education"},
        {UiButtonIds::ParksAndRecreationMenuButton, "Parks and Recreation", "ui/toolbar/btn_parks_and_recreation"},
        {UiButtonIds::RewardsMenuButton, "Rewards", "ui/toolbar/btn_rewards"},
        {UiButtonIds::LandmarksMenuButton, "Landmarks", "ui/toolbar/btn_landmarks"}
    }};

    constexpr std::array<UiToolButtonDefinition, 3> Police{{
        {UiButtonIds::SmallPoliceStationButton, "Small Police Station", "ui/toolbar/btn_small_police_station"},
        {UiButtonIds::LargePoliceStationButton, "Large Police Station", "ui/toolbar/btn_large_police_station"},
        {UiButtonIds::JailButton, "Jail", "ui/toolbar/btn_jail"}
    }};

    constexpr std::array<UiToolButtonDefinition, 2> Fire{{
        {UiButtonIds::SmallFireStationButton, "Small Fire Station", "ui/toolbar/btn_small_fire_station"},
        {UiButtonIds::LargeFireStationButton, "Large Fire Station", "ui/toolbar/btn_large_fire_station"}
    }};

    constexpr std::array<UiToolButtonDefinition, 2> Health{{
        {UiButtonIds::ClinicButton, "Clinic", "ui/toolbar/btn_clinic"},
        {UiButtonIds::HospitalButton, "Hospital", "ui/toolbar/btn_hospital"}
    }};

    constexpr std::array<UiToolButtonDefinition, 6> Education{{
        {UiButtonIds::SmallSchoolButton, "Small School", "ui/toolbar/btn_small_school"},
        {UiButtonIds::LargeSchoolButton, "Large School", "ui/toolbar/btn_large_school"},
        {UiButtonIds::CollegeButton, "College", "ui/toolbar/btn_college"},
        {UiButtonIds::LibraryButton, "Library", "ui/toolbar/btn_library"},
        {UiButtonIds::CentralLibraryButton, "Central Library", "ui/toolbar/btn_central_library"},
        {UiButtonIds::MuseumButton, "Museum", "ui/toolbar/btn_museum"}
    }};

    // Read left to right, then down: four columns, with a partially filled eighth row.
    constexpr std::array<UiToolButtonDefinition, 29> ParksAndRecreation{{
        {UiButtonIds::LawnButton, "Lawn", "ui/toolbar/btn_lawn"},
        {UiButtonIds::PavedAreaButton, "Paved Area", "ui/toolbar/btn_paved_area"},
        {UiButtonIds::CommunityGardenButton, "Community Garden", "ui/toolbar/btn_community_garden"},
        {UiButtonIds::ButterflyGardenButton, "Butterfly Garden", "ui/toolbar/btn_butterfly_garden"},
        {UiButtonIds::FlowerBedButton, "Flower Bed", "ui/toolbar/btn_flower_bed"},
        {UiButtonIds::SmallParkButton, "Small Park", "ui/toolbar/btn_small_park"},
        {UiButtonIds::GazeboButton, "Gazebo", "ui/toolbar/btn_gazebo"},
        {UiButtonIds::SmallFountainButton, "Small Fountain", "ui/toolbar/btn_small_fountain"},
        {UiButtonIds::SculptureButton, "Sculpture", "ui/toolbar/btn_sculpture"},
        {UiButtonIds::EternalFlameButton, "Eternal Flame", "ui/toolbar/btn_eternal_flame"},
        {UiButtonIds::ClockTowerButton, "Clock Tower", "ui/toolbar/btn_clock_tower"},
        {UiButtonIds::BeachButton, "Beach", "ui/toolbar/btn_beach"},
        {UiButtonIds::PierButton, "Pier", "ui/toolbar/btn_pier"},
        {UiButtonIds::TennisCourtButton, "Tennis Court", "ui/toolbar/btn_tennis_court"},
        {UiButtonIds::BasketballCourtButton, "Basketball Court", "ui/toolbar/btn_basketball_court"},
        {UiButtonIds::PlaygroundButton, "Playground", "ui/toolbar/btn_playground"},
        {UiButtonIds::PoolButton, "Pool", "ui/toolbar/btn_swimming_pool"},
        {UiButtonIds::SkateboardParkButton, "Skateboard Park", "ui/toolbar/btn_skateboard_park"},
        {UiButtonIds::MediumParkButton, "Medium Park", "ui/toolbar/btn_medium_park"},
        {UiButtonIds::MediumPlazaButton, "Medium Plaza", "ui/toolbar/btn_med_plaza"},
        {UiButtonIds::PondButton, "Pond", "ui/toolbar/btn_pond"},
        {UiButtonIds::LargeFountainButton, "Large Fountain", "ui/toolbar/btn_large_fountain"},
        {UiButtonIds::SoccerFieldButton, "Soccer Field", "ui/toolbar/btn_soccer_field"},
        {UiButtonIds::SoftballFieldButton, "Softball Field", "ui/toolbar/btn_softball_field"},
        {UiButtonIds::MarinaButton, "Marina", "ui/toolbar/btn_marina"},
        {UiButtonIds::ZooButton, "Zoo", "ui/toolbar/btn_zoo"},
        {UiButtonIds::AmphitheaterButton, "Amphitheater", "ui/toolbar/btn_amphitheater"},
        {UiButtonIds::GiantParkButton, "Giant Park", "ui/toolbar/btn_giant_park"},
        {UiButtonIds::MiniGolfButton, "Mini Golf", "ui/toolbar/btn_mini_golf"}
    }};

    // Replace these individual entries as real rewards become available.
    constexpr std::array<UiToolButtonDefinition, 20> Rewards{{
        {UiButtonIds::RewardButton1, "Reward 1 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton2, "Reward 2 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton3, "Reward 3 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton4, "Reward 4 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton5, "Reward 5 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton6, "Reward 6 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton7, "Reward 7 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton8, "Reward 8 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton9, "Reward 9 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton10, "Reward 10 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton11, "Reward 11 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton12, "Reward 12 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton13, "Reward 13 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton14, "Reward 14 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton15, "Reward 15 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton16, "Reward 16 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton17, "Reward 17 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton18, "Reward 18 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton19, "Reward 19 (Placeholder)", "ui/toolbar/btn_rewards"},
        {UiButtonIds::RewardButton20, "Reward 20 (Placeholder)", "ui/toolbar/btn_rewards"}
    }};

    // Replace these individual entries as real landmarks become available.
    constexpr std::array<UiToolButtonDefinition, 20> Landmarks{{
        {UiButtonIds::LandmarkButton1, "Landmark 1 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton2, "Landmark 2 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton3, "Landmark 3 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton4, "Landmark 4 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton5, "Landmark 5 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton6, "Landmark 6 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton7, "Landmark 7 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton8, "Landmark 8 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton9, "Landmark 9 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton10, "Landmark 10 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton11, "Landmark 11 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton12, "Landmark 12 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton13, "Landmark 13 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton14, "Landmark 14 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton15, "Landmark 15 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton16, "Landmark 16 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton17, "Landmark 17 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton18, "Landmark 18 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton19, "Landmark 19 (Placeholder)", "ui/toolbar/btn_landmarks"},
        {UiButtonIds::LandmarkButton20, "Landmark 20 (Placeholder)", "ui/toolbar/btn_landmarks"}
    }};

    constexpr std::array<UiToolMenuDefinition, 7> Menus{{
        {UiButtonIds::PoliceMenuButton, Police},
        {UiButtonIds::FireMenuButton, Fire},
        {UiButtonIds::HealthMenuButton, Health},
        {UiButtonIds::EducationMenuButton, Education},
        {UiButtonIds::ParksAndRecreationMenuButton, ParksAndRecreation, 4},
        {UiButtonIds::RewardsMenuButton, Rewards, 4},
        {UiButtonIds::LandmarksMenuButton, Landmarks, 4}
    }};
}

void CreateGovernmentButtons(UiButtons& buttons, TextureManager& textures)
{
    AddToolFlyout(buttons, textures, UiButtonIds::GovernmentButton, Categories, UiButtonShape::Circle);
    for (const auto& menu : Menus)
        AddToolFlyout(buttons, textures, menu.trigger, menu.buttons, UiButtonShape::Rectangle);
}

void LayoutGovernmentButtons(UiButtons& buttons, const UiLayout& layout)
{
    LayoutToolFlyouts(buttons, layout, UiButtonIds::GovernmentButton, Categories, Menus);
}
