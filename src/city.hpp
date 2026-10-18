#pragma once

#include <string>
#include <chrono>

class City {
    public:
        std::string name;
        unsigned int population = 0;
        unsigned int funds = 0;
        std::chrono::sys_days date { std::chrono::year{1900} / std::chrono::January / 1 };
};