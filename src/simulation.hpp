#pragma once

#include "city.hpp"

class Simulation 
{
public:
    void Update(City& city, double elapsedMilliseconds);

private:
    double timeAccumulated = 0.0;
};