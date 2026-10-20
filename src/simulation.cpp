#include "simulation.hpp"
#include <chrono>
#include <algorithm>

#define ACCUMULATION_THRESHOLD_SPEED4 100
#define ACCUMULATION_THRESHOLD_SPEED3 300
#define ACCUMULATION_THRESHOLD_SPEED2 1000
#define ACCUMULATION_THRESHOLD_SPEED1 2000

void Simulation::SetSimulationSpeed(int speed)
{
    simulationSpeed = std::clamp(speed, 0, 4);

    if (simulationSpeed > 0)
    {
        lastRunningSpeed = simulationSpeed;
    }
}

int Simulation::GetSimulationSpeed() const
{
    return simulationSpeed;
}

void Simulation::Resume()
{
    SetSimulationSpeed(lastRunningSpeed);
}

void Simulation::Update(City& city, double elapsedMilliseconds)
{
    if(simulationSpeed == 0)
    {
        return;
    }

    timeAccumulated += elapsedMilliseconds;

    switch(simulationSpeed)
    {
        case 0:
        {
            break;
        }
        // Shuffle
        case 1:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED1)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        // Stroll
        case 2:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED2)
            {
                city.date += std::chrono::days{1};
                timeAccumulated = 0.0;
            }
            break;
        }
        // Canter
        case 3:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED3 * 3)
            {
                city.date += std::chrono::days{3};
                timeAccumulated = 0.0;
            }
            break;
        }
        // Dash
        case 4:
        {
            if(timeAccumulated > ACCUMULATION_THRESHOLD_SPEED4 * 6)
            {
                city.date += std::chrono::days{6};
                timeAccumulated = 0.0;
            }
            break;
        }
        default:
            break;
    }
}