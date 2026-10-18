#pragma once

#include "city.hpp"

class Simulation 
{
public:
    void Update(City& city, double elapsedMilliseconds);
    void SetSimulationSpeed(int speed);
    int GetSimulationSpeed(void);

private:
    double timeAccumulated = 0.0;
    int simulationSpeed = 0;
};