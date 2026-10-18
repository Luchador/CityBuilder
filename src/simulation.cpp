#include "simulation.hpp"
#include <chrono>

#define ACCUMULATION_THRESHOLD_SPEED4 100
#define ACCUMULATION_THRESHOLD_SPEED3 300
#define ACCUMULATION_THRESHOLD_SPEED2 1000
#define ACCUMULATION_THRESHOLD_SPEED1 2000

void Simulation::Update(City& city, double elapsedMilliseconds)
{
    timeAccumulated += elapsedMilliseconds;

    switch(simulationSpeed)
    {
        case 0:
        {
            break;
        }
        case 1:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED1)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        case 2:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED2)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        case 3:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED3)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        case 4:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED4)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        default:
            break;
    }
}