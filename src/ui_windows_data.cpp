#include "ui_manager.hpp"

void UiManager::CreateNewCityWindow()
{
    UiWindow& createCity = AddWindow("Create New City", {(layout.width - 540) * 0.5f, (layout.height - 420) * 0.5f, 580, 520});
    createCityWindowId = createCity.GetId();

    createCity.AddLabel(1, {0, 0, 380, 22}, "City Name");
    createCity.AddTextBox(2, {0, 26, 400, 34}, "New City");
    createCity.AddLabel(3, {0, 72, 380, 22}, "Mayor Name");
    createCity.AddTextBox(4, {0, 98, 400, 34}, "El Jefe");

    createCity.AddLabel(6, {0, 148, 480, 22}, "Difficulty");
    createCity.AddRadioButton(7, 1, {0, 178, 150, 28}, "Easy");
    createCity.AddRadioButton(8, 1, {120, 178, 150, 28}, "Medium", true);
    createCity.AddRadioButton(9, 1, {240, 178, 150, 28}, "Hard");

    createCity.AddLabel(10, {0, 218, 480, 22}, "Start Year");
    createCity.AddRadioButton(11, 1, {0, 248, 150, 28}, "1900");
    createCity.AddRadioButton(12, 1, {120, 248, 150, 28}, "1950", true);
    createCity.AddRadioButton(13, 1, {240, 248, 150, 28}, "2000");

    createCity.AddLabel(14, {0, 288, 480, 22}, "Map Size ");
    createCity.AddRadioButton(15, 1, {0, 318, 150, 28}, "Small");
    createCity.AddRadioButton(16, 1, {120, 318, 150, 28}, "Medium", true);
    createCity.AddRadioButton(17, 1, {240, 318, 150, 28}, "Large");
    createCity.AddRadioButton(18, 1, {360, 318, 150, 28}, "Giant");

    createCity.AddFooterButton(19, "Accept", [](UiWindow& panel) { panel.Close(); });
    createCity.Close();

    FocusWindow(nullptr);
}
