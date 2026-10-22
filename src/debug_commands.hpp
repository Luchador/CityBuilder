#pragma once

#include <string>
#include <string_view>

class City;

// Executes on the main thread and returns one feedback line for the console.
// A null city is allowed, so commands can report unavailable game state.
std::string ExecuteDebugCommand(std::string_view command, City* city);
