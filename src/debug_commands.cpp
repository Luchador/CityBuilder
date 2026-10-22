#include "debug_commands.hpp"
#include "city.hpp"

#include <cstdint>
#include <limits>

std::string ExecuteDebugCommand(std::string_view command, City* city)
{
    const auto first = command.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "Enter a command.";
    command = command.substr(first, command.find_last_not_of(" \t\r\n") - first + 1);

    // Command names use case-insensitive ASCII; no locale or shell execution.
    std::string name(command);
    for (char& letter : name)
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');

    // Add future commands here; their UI always uses the same submit callback.
    if (name == "cooper airways")
    {
        if (!city) return "Create a city before using this command.";
        constexpr std::int64_t grant = 1'000'000;
        if (city->funds > std::numeric_limits<std::int64_t>::max() - grant)
            return "Cannot add funds: the maximum balance would be exceeded.";

        city->funds += grant;
        return "Added $1,000,000 to city funds.";
    }

    return "Unknown command.";
}
