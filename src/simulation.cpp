#include "simulation.hpp"
#include <chrono>

void Simulation::Update(City& city, double elapsedMilliseconds)
{
    city.date += std::chrono::days{1};
}