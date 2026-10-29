#include "ui_debug_console.hpp"

#include <utility>

UiDebugConsole::UiDebugConsole(UiWindow& panel, CommandHandler executeCommand) : window(panel)
{
    const float width = window.GetContentBounds().w;

    window.AddTextBox(CommandInput, {0, 0, width, 36}, "", 128);
    window.AddLabel(Feedback, {0, 48, width, 28}, "Enter a command and press Enter.");

    window.SetTextBoxSubmitHandler(CommandInput, [execute = std::move(executeCommand)](UiWindow& panel)
    {
        // Own a copy so clearing/editing the field cannot invalidate the command.
        const std::string command = panel.GetText(CommandInput);
        const std::string response = execute ? execute(command) : "Debug commands are unavailable.";
        panel.SetText(Feedback, response);
        panel.SetText(CommandInput, "");
        panel.FocusControl(CommandInput);
    });
}

void UiDebugConsole::FocusInput()
{
    window.FocusControl(CommandInput, true);
}
