#include "simulation.hpp"
#include <chrono>

void Simulation::Update(City& city, double elapsedMilliseconds)
{
    timeAccumulated += elapsedMilliseconds;

    if(timeAccumulated > 1000.0)
    {
        city.date += std::chrono::days{1};
        timeAccumulated = 0.0;
    }
}