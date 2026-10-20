#include "ui_manager.hpp"

void UiManager::CreateNewCityWindow(const City& city)
{
    auto& demo = AddWindow("Create New City", {(layout.width - 540) * 0.5f, (layout.height - 420) * 0.5f, 540, 420});
    demoWindowId = demo.GetId();
    demo.AddLabel(1, {0, 0, 380, 22}, "City Name");
    demo.AddTextBox(2, {0, 26, 400, 34}, city.name);
    demo.AddLabel(3, {0, 72, 380, 22}, "Mayor Name");
    demo.AddTextBox(4, {0, 98, 400, 34}, city.mayor_name);

    demo.AddLabel(6, {0, 148, 480, 22}, "Difficulty");
    demo.AddRadioButton(7, 1, {0, 178, 150, 28}, "Easy", true);
    demo.AddRadioButton(8, 1, {170, 178, 150, 28}, "Medium");
    demo.AddRadioButton(9, 1, {340, 178, 150, 28}, "Hard");

    demo.AddLabel(10, {0, 218, 480, 22}, "Start Year");
    demo.AddRadioButton(11, 1, {0, 248, 150, 28}, "1900", true);
    demo.AddRadioButton(12, 1, {170, 248, 150, 28}, "1950");
    demo.AddRadioButton(13, 1, {340, 248, 150, 28}, "2000");

    demo.AddFooterButton(14, "Accept", [](UiWindow& panel) { panel.Close(); });
    demo.Close();

    FocusWindow(nullptr);
}
