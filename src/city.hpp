#pragma once

#include <string>
#include <chrono>
#include <cstdint>

class City {
    public:
        std::string name;
        std::string mayor_name = "El Jefe";
        unsigned int population = 0;
        std::int64_t funds = 0;
        std::chrono::sys_days date { std::chrono::year{1900} / std::chrono::January / 1 };
};