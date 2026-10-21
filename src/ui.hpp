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

    inline constexpr UiButtonId LightResidentialButton = 210;
    inline constexpr UiButtonId MediumResidentialButton = 211;
    inline constexpr UiButtonId DenseResidentialButton = 212;
    inline constexpr UiButtonId LightCommercialButton = 220;
    inline constexpr UiButtonId MediumCommercialButton = 221;
    inline constexpr UiButtonId DenseCommercialButton = 222;
    inline constexpr UiButtonId FarmButton = 230;
    inline constexpr UiButtonId MediumIndustrialButton = 231;
    inline constexpr UiButtonId HeavyIndustrialButton = 232;

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
    inline constexpr UiButtonId PedestrianButton = 404;
    inline constexpr UiButtonId SeaMenuButton = 405;
    inline constexpr UiButtonId AirportMenuButton = 406;

    inline constexpr UiButtonId DirtRoadButton = 410;
    inline constexpr UiButtonId RoadButton = 411;
    inline constexpr UiButtonId AvenueButton = 412;
    inline constexpr UiButtonId OneWayRoadButton = 413;
    inline constexpr UiButtonId TramButton = 414;
    inline constexpr UiButtonId TramStationButton = 415;
    inline constexpr UiButtonId HighwayButton = 420;
    inline constexpr UiButtonId ThreeWayInterchangeButton = 421;
    inline constexpr UiButtonId CloverLeafInterchangeButton = 422;
    inline constexpr UiButtonId OverpassButton = 423;
    inline constexpr UiButtonId OnRampButton = 424;
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

    inline constexpr UiButtonId SidewalkButton = 447;
    inline constexpr UiButtonId MultiUsePath = 448;

    inline constexpr UiButtonId FerryButton = 450;
    inline constexpr UiButtonId SeaportButton = 451;
    inline constexpr UiButtonId AirportButton = 460;
    inline constexpr UiButtonId RunwayButton = 461;
    inline constexpr UiButtonId TarmacButton = 462;
    inline constexpr UiButtonId TerminalButton = 463;

    inline constexpr UiButtonId WaterMenuButton = 500;
    inline constexpr UiButtonId PowerMenuButton = 501;
    inline constexpr UiButtonId GarbageMenuButton = 502;

    inline constexpr UiButtonId WaterPipeButton = 510;
    inline constexpr UiButtonId WaterTowerButton = 511;
    inline constexpr UiButtonId WaterPumpButton = 512;
    inline constexpr UiButtonId DesalinationPlantButton = 513;
    inline constexpr UiButtonId WaterTreatmentPlantButton = 514;

    inline constexpr UiButtonId PowerLineButton = 520;
    inline constexpr UiButtonId UndergroundLineButton = 521;
    inline constexpr UiButtonId CoalPowerPlantButton = 522;
    inline constexpr UiButtonId OilPowerPlantButton = 523;
    inline constexpr UiButtonId GasPowerPlantButton = 524;
    inline constexpr UiButtonId WindTurbineButton = 525;
    inline constexpr UiButtonId SolarPowerPlantButton = 526;
    inline constexpr UiButtonId NuclearPowerPlantButton = 527;
    inline constexpr UiButtonId MicrowavePowerPlantButton = 528;
    inline constexpr UiButtonId FusionPowerPlantButton = 529;

    inline constexpr UiButtonId LandfillButton = 530;
    inline constexpr UiButtonId IncineratorButton = 531;
    inline constexpr UiButtonId RecyclingCenterButton = 532;
    inline constexpr UiButtonId WasteToEnergyPlantButton = 533;

    inline constexpr UiButtonId PoliceMenuButton = 600;
    inline constexpr UiButtonId FireMenuButton = 601;
    inline constexpr UiButtonId HealthMenuButton = 602;
    inline constexpr UiButtonId EducationMenuButton = 603;
    inline constexpr UiButtonId ParksAndRecreationMenuButton = 604;
    inline constexpr UiButtonId RewardsMenuButton = 605;
    inline constexpr UiButtonId LandmarksMenuButton = 606;

    inline constexpr UiButtonId SmallPoliceStationButton = 610;
    inline constexpr UiButtonId LargePoliceStationButton = 611;
    inline constexpr UiButtonId JailButton = 612;

    inline constexpr UiButtonId SmallFireStationButton = 620;
    inline constexpr UiButtonId LargeFireStationButton = 621;

    inline constexpr UiButtonId ClinicButton = 630;
    inline constexpr UiButtonId HospitalButton = 631;

    inline constexpr UiButtonId SmallSchoolButton = 640;
    inline constexpr UiButtonId LargeSchoolButton = 641;
    inline constexpr UiButtonId CollegeButton = 642;
    inline constexpr UiButtonId LibraryButton = 643;
    inline constexpr UiButtonId CentralLibraryButton = 644;
    inline constexpr UiButtonId MuseumButton = 645;

    inline constexpr UiButtonId LawnButton = 660;
    inline constexpr UiButtonId PavedAreaButton = 661;
    inline constexpr UiButtonId CommunityGardenButton = 662;
    inline constexpr UiButtonId ButterflyGardenButton = 663;
    inline constexpr UiButtonId FlowerBedButton = 664;
    inline constexpr UiButtonId SplashParkButton = 665;
    inline constexpr UiButtonId SmallParkButton = 666;
    inline constexpr UiButtonId GazeboButton = 667;
    inline constexpr UiButtonId SmallFountainButton = 668;
    inline constexpr UiButtonId SculptureButton = 669;
    inline constexpr UiButtonId EternalFlameButton = 670;
    inline constexpr UiButtonId ClockTowerButton = 671;
    inline constexpr UiButtonId BeachButton = 672;
    inline constexpr UiButtonId PierButton = 673;
    inline constexpr UiButtonId TennisCourtButton = 674;
    inline constexpr UiButtonId BasketballCourtButton = 675;
    inline constexpr UiButtonId PlaygroundButton = 676;
    inline constexpr UiButtonId PoolButton = 677;
    inline constexpr UiButtonId SkateboardParkButton = 678;
    inline constexpr UiButtonId MediumParkButton = 679;
    inline constexpr UiButtonId MediumPlazaButton = 680;
    inline constexpr UiButtonId PondButton = 681;
    inline constexpr UiButtonId LargeFountainButton = 682;
    inline constexpr UiButtonId LargePlazaButton = 683;
    inline constexpr UiButtonId LargeParkButton = 684;
    inline constexpr UiButtonId SoccerFieldButton = 685;
    inline constexpr UiButtonId SoftballFieldButton = 686;
    inline constexpr UiButtonId MarinaButton = 687;
    inline constexpr UiButtonId ZooButton = 688;
    inline constexpr UiButtonId AmphitheaterButton = 689;
    inline constexpr UiButtonId GiantParkButton = 690;
    inline constexpr UiButtonId MiniGolfButton = 691;

    inline constexpr UiButtonId RewardButton1 = 700;
    inline constexpr UiButtonId RewardButton2 = 701;
    inline constexpr UiButtonId RewardButton3 = 702;
    inline constexpr UiButtonId RewardButton4 = 703;
    inline constexpr UiButtonId RewardButton5 = 704;
    inline constexpr UiButtonId RewardButton6 = 705;
    inline constexpr UiButtonId RewardButton7 = 706;
    inline constexpr UiButtonId RewardButton8 = 707;
    inline constexpr UiButtonId RewardButton9 = 708;
    inline constexpr UiButtonId RewardButton10 = 709;
    inline constexpr UiButtonId RewardButton11 = 710;
    inline constexpr UiButtonId RewardButton12 = 711;
    inline constexpr UiButtonId RewardButton13 = 712;
    inline constexpr UiButtonId RewardButton14 = 713;
    inline constexpr UiButtonId RewardButton15 = 714;
    inline constexpr UiButtonId RewardButton16 = 715;
    inline constexpr UiButtonId RewardButton17 = 716;
    inline constexpr UiButtonId RewardButton18 = 717;
    inline constexpr UiButtonId RewardButton19 = 718;
    inline constexpr UiButtonId RewardButton20 = 719;

    inline constexpr UiButtonId LandmarkButton1 = 720;
    inline constexpr UiButtonId LandmarkButton2 = 721;
    inline constexpr UiButtonId LandmarkButton3 = 722;
    inline constexpr UiButtonId LandmarkButton4 = 723;
    inline constexpr UiButtonId LandmarkButton5 = 724;
    inline constexpr UiButtonId LandmarkButton6 = 725;
    inline constexpr UiButtonId LandmarkButton7 = 726;
    inline constexpr UiButtonId LandmarkButton8 = 727;
    inline constexpr UiButtonId LandmarkButton9 = 728;
    inline constexpr UiButtonId LandmarkButton10 = 729;
    inline constexpr UiButtonId LandmarkButton11 = 730;
    inline constexpr UiButtonId LandmarkButton12 = 731;
    inline constexpr UiButtonId LandmarkButton13 = 732;
    inline constexpr UiButtonId LandmarkButton14 = 733;
    inline constexpr UiButtonId LandmarkButton15 = 734;
    inline constexpr UiButtonId LandmarkButton16 = 735;
    inline constexpr UiButtonId LandmarkButton17 = 736;
    inline constexpr UiButtonId LandmarkButton18 = 737;
    inline constexpr UiButtonId LandmarkButton19 = 738;
    inline constexpr UiButtonId LandmarkButton20 = 739;

    inline constexpr UiButtonId RaiseTerrainButton = 800;
    inline constexpr UiButtonId LowerTerrainButton = 801;
    inline constexpr UiButtonId FlattenTerrainButton = 802;
    inline constexpr UiButtonId CreateWaterButton = 803;
    inline constexpr UiButtonId PlantFoliageMenuButton = 804;
    inline constexpr UiButtonId CreateAnimalMenuButton = 805;

    inline constexpr UiButtonId DeciduousButton = 810;
    inline constexpr UiButtonId EvergreenButton = 811;
    inline constexpr UiButtonId RedwoodButton = 812;
    inline constexpr UiButtonId PrairieButton = 813;
    inline constexpr UiButtonId ChaparralButton = 814;
    inline constexpr UiButtonId XerophytesButton = 815;
    inline constexpr UiButtonId SubtropicalButton = 816;

    inline constexpr UiButtonId BisonButton = 830;
    inline constexpr UiButtonId MooseButton = 831;
    inline constexpr UiButtonId DeerButton = 832;
    inline constexpr UiButtonId BearButton = 833;
    inline constexpr UiButtonId WolfButton = 834;
    inline constexpr UiButtonId JavelinaButton = 835;
    inline constexpr UiButtonId BighornSheepButton = 836;
    inline constexpr UiButtonId FoxButton = 837;
    inline constexpr UiButtonId CougarButton = 838;
    inline constexpr UiButtonId BobcatButton = 839;
    inline constexpr UiButtonId BeaverButton = 840;
    inline constexpr UiButtonId DolphinButton = 841;
    inline constexpr UiButtonId SharkButton = 842;
    inline constexpr UiButtonId AlligatorButton = 843;
    inline constexpr UiButtonId RoadrunnerButton = 844;
    inline constexpr UiButtonId RavenButton = 845;
    inline constexpr UiButtonId CaliforniaCondorButton = 846;
    inline constexpr UiButtonId BaldEagleButton = 847;
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
// Flyout definitions and single-icon assignments live in these modules.
void CreateTerrainButtons(UiButtons& buttons, TextureManager& textures);
void LayoutTerrainButtons(UiButtons& buttons, const UiLayout& layout);
void CreateZoningButtons(UiButtons& buttons, TextureManager& textures);
void LayoutZoningButtons(UiButtons& buttons, const UiLayout& layout);
void CreateTransportationButtons(UiButtons& buttons, TextureManager& textures);
void LayoutTransportationButtons(UiButtons& buttons, const UiLayout& layout);
void CreateUtilityButtons(UiButtons& buttons, TextureManager& textures);
void LayoutUtilityButtons(UiButtons& buttons, const UiLayout& layout);
void CreateGovernmentButtons(UiButtons& buttons, TextureManager& textures);
void LayoutGovernmentButtons(UiButtons& buttons, const UiLayout& layout);
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
