#include "city.hpp"

#include <stdexcept>

std::uint32_t GetMapTileCount(CityMapSize size)
{
    switch (size)
    {
        case CityMapSize::Small: return 128;
        case CityMapSize::Medium: return 256;
        case CityMapSize::Large: return 512;
        case CityMapSize::Giant: return 1024;
    }

    throw std::invalid_argument("Choose a valid map size.");
}

void CityCreationOptions::Validate() const
{
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw std::invalid_argument("Please enter a city name.");
    }

    if (mayorName.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw std::invalid_argument("Please enter a mayor name.");
    }

    if (startingYear != 1900 && startingYear != 1950 && startingYear != 2000)
    {
        throw std::invalid_argument("Choose 1900, 1950, or 2000 as the starting year.");
    }

    if (difficulty != CityDifficulty::Easy && difficulty != CityDifficulty::Medium && difficulty != CityDifficulty::Hard)
    {
        throw std::invalid_argument("Choose a valid difficulty.");
    }

    if (mapSize != CityMapSize::Small && mapSize != CityMapSize::Medium && mapSize != CityMapSize::Large && mapSize != CityMapSize::Giant)
    {
        throw std::invalid_argument("Choose a valid map size.");
    }
}

void City::CreateCity(const CityCreationOptions& options)
{
    options.Validate();
    name = options.name;
    mayor_name = options.mayorName;
    difficulty = options.difficulty;
    mapSize = options.mapSize;
    population = 0;

    if(difficulty == CityDifficulty::Easy)
    {
        funds = 100000;
    }
    else if (difficulty == CityDifficulty::Medium)
    {
        funds = 50000;
    }
    else
    {
        funds = 25000;
    }

    date = std::chrono::sys_days{std::chrono::year{options.startingYear} / std::chrono::January / 1};
}
