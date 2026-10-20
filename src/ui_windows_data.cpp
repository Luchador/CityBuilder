#include "ui_manager.hpp"

void UiManager::CreateNewCityWindow()
{
    UiWindow& createCity = AddWindow("Create New City", {(layout.width - 540) * 0.5f, (layout.height - 420) * 0.5f, 540, 420});
    createCityWindowId = createCity.GetId();

    createCity.AddLabel(1, {0, 0, 380, 22}, "City Name");
    createCity.AddTextBox(2, {0, 26, 400, 34}, "New City");
    createCity.AddLabel(3, {0, 72, 380, 22}, "Mayor Name");
    createCity.AddTextBox(4, {0, 98, 400, 34}, "El Jefe");

    createCity.AddLabel(6, {0, 148, 480, 22}, "Difficulty");
    createCity.AddRadioButton(7, 1, {0, 178, 150, 28}, "Easy", true);
    createCity.AddRadioButton(8, 1, {170, 178, 150, 28}, "Medium");
    createCity.AddRadioButton(9, 1, {340, 178, 150, 28}, "Hard");

    createCity.AddLabel(10, {0, 218, 480, 22}, "Start Year");
    createCity.AddRadioButton(11, 1, {0, 248, 150, 28}, "1900", true);
    createCity.AddRadioButton(12, 1, {170, 248, 150, 28}, "1950");
    createCity.AddRadioButton(13, 1, {340, 248, 150, 28}, "2000");

    createCity.AddFooterButton(14, "Accept", [](UiWindow& panel) { panel.Close(); });
    createCity.Close();

    FocusWindow(nullptr);
}
