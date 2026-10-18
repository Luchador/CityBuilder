#pragma once

#include "city.hpp"

class Simulation 
{
public:
    void Update(City& city, double elapsedMilliseconds);
    void SetSimulationSpeed(int speed);
    int GetSimulationSpeed() const;
    void Resume();

private:
    double timeAccumulated = 0.0;
    int simulationSpeed = 0; // 0 = paused; 1–4 = running speeds.
    int lastRunningSpeed = 1;
};