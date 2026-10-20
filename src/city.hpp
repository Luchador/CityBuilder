#pragma once

#include <string>
#include <chrono>
#include <cstdint>

enum class CityDifficulty { Easy, Medium, Hard };
enum class CityMapSize { Small, Medium, Large, Giant };

// Convert the creation form's choice to the number of tiles on each side.
std::uint32_t GetMapTileCount(CityMapSize size);

// Values chosen by the creation form.
struct CityCreationOptions
{
    std::string name = "New City";
    std::string mayorName = "El Jefe";
    CityDifficulty difficulty = CityDifficulty::Medium;
    CityMapSize mapSize = CityMapSize::Medium;
    int startingYear = 1950;

    void Validate() const;
};

class City {
    public:
        City() = default;
        explicit City(const CityCreationOptions& options) { CreateCity(options); }
        std::string name = "New City";
        std::string mayor_name = "El Jefe";
        unsigned int population = 0;
        std::int64_t funds = 0;
        std::chrono::sys_days date { std::chrono::year{1900} / std::chrono::January / 1 };

        CityDifficulty difficulty = CityDifficulty::Medium;
        CityMapSize mapSize = CityMapSize::Medium;

        void CreateCity(const CityCreationOptions& options);
};
