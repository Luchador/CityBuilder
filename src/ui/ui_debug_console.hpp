#pragma once

#include "ui_window.hpp"

#include <functional>
#include <string>
#include <string_view>

// UiManager owns the UiWindow; this companion sets up its console controls.
class UiDebugConsole
{
public:
    using CommandHandler = std::function<std::string(std::string_view)>;
    UiDebugConsole(UiWindow& window, CommandHandler executeCommand);
    UiDebugConsole(const UiDebugConsole&) = delete;
    UiDebugConsole& operator=(const UiDebugConsole&) = delete;
    UiWindowId GetWindowId() const { return window.GetId(); }
    void FocusInput();

private:
    static constexpr UiControlId CommandInput = 1;
    static constexpr UiControlId Feedback = 2;
    UiWindow& window;
};
