#include "ui_manager.hpp"

#include <stdexcept>

namespace
{
    std::string TrimName(const std::string& value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");

        if (begin == std::string::npos) 
        {
            return {};
        }

        return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
    }
}

void UiManager::CreateNewCityWindow()
{
    UiWindow& createCity = AddWindow("Create New City", {(layout.width - 580) * 0.5f, (layout.height - 520) * 0.5f, 580, 520});
    createCityWindowId = createCity.GetId();
    createCity.SetClosable(false);
    const CityCreationOptions defaults;

    // City and Mayor name fields.
    createCity.AddLabel(1, {0, 0, 380, 22}, "City Name");
    createCity.AddTextBox(2, {0, 26, 400, 34}, defaults.name);
    createCity.AddLabel(3, {0, 72, 380, 22}, "Mayor Name");
    createCity.AddTextBox(4, {0, 98, 400, 34}, defaults.mayorName);

    // Difficulty options
    createCity.AddLabel(6, {0, 148, 480, 22}, "Difficulty");
    createCity.AddRadioButton(7, 1, {0, 178, 140, 28}, "Easy ($100k)");
    createCity.AddRadioButton(8, 1, {160, 178, 140, 28}, "Medium ($50k)", true);
    createCity.AddRadioButton(9, 1, {320, 178, 140, 28}, "Hard ($25k)");

    // Starting year options
    createCity.AddLabel(10, {0, 218, 480, 22}, "Start Year");
    createCity.AddRadioButton(11, 2, {0, 248, 100, 28}, "1900");
    createCity.AddRadioButton(12, 2, {120, 248, 100, 28}, "1950", true);
    createCity.AddRadioButton(13, 2, {240, 248, 100, 28}, "2000");

    // Map size options
    createCity.AddLabel(14, {0, 288, 480, 22}, "Map Size ");
    createCity.AddRadioButton(15, 3, {0, 318, 100, 28}, "Small");
    createCity.AddRadioButton(16, 3, {120, 318, 100, 28}, "Medium", true);
    createCity.AddRadioButton(17, 3, {240, 318, 100, 28}, "Large");
    createCity.AddRadioButton(18, 3, {360, 318, 100, 28}, "Giant");

    TextStyle errorStyle;
    errorStyle.size = 14;
    errorStyle.wrap = true;
    errorStyle.color = {1.0f, 0.75f, 0.65f, 1.0f};

    createCity.AddLabel(20, {0, 362, 548, 44}, "", errorStyle);
    createCity.AddFooterButton(19, "Accept", [this](UiWindow& panel)
    {
        if (!cityCreationPending) 
        {
            return;
        }
        try
        {
            CityCreationOptions options;
            options.name = TrimName(panel.GetText(2));
            options.mayorName = TrimName(panel.GetText(4));
            options.difficulty = panel.IsChecked(7) ? CityDifficulty::Easy : panel.IsChecked(9) ? CityDifficulty::Hard : CityDifficulty::Medium;
            options.startingYear = panel.IsChecked(11) ? 1900 : panel.IsChecked(13) ? 2000 : 1950;
            options.mapSize = panel.IsChecked(15) ? CityMapSize::Small : panel.IsChecked(17) ? CityMapSize::Large : panel.IsChecked(18) ? CityMapSize::Giant : CityMapSize::Medium;
            options.Validate();
    
            if (!actions.createCity) 
            {
                throw std::runtime_error("City creation is not available.");
            }

            actions.createCity(options);
            panel.SetText(20, "");
            cityCreationPending = false;
            panel.Close();
        }
        catch (const std::exception& error)
        {
            SDL_Log("Create city: %s", error.what());
            panel.SetText(20, error.what());
        }
    });
}

void UiManager::CreateMeetWindow()
{
    UiWindow& meet = AddWindow("Meet with Advisors", {(layout.width - 580) * 0.5f, (layout.height - 520) * 0.5f, 580, 520});
    meetWindowId = meet.GetId();

    // Start hidden until the player opens it.
    meet.Close();
}
