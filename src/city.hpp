#pragma once

#include <chrono>

class City {
    public:
        unsigned int GetPopulation();
        void SetPopulation(unsigned int pop);
        unsigned int GetFunds();
        void SetFunds(unsigned int amount);
        std::chrono::sys_days date { std::chrono::year{1900} / std::chrono::January / 1 };

    private:
        unsigned int population = 0;
        unsigned int funds = 0;
};