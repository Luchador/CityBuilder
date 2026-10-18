#include "city.hpp"

unsigned int City::GetPopulation()
{
    return population;
}

void City::SetPopulation(unsigned int pop)
{
    population = pop;
}

unsigned int City::GetFunds()
{
    return funds;
}

void City::SetFunds(unsigned int amount)
{
    funds = amount;
}