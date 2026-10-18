#pragma once

class City {
    public:
        unsigned int GetPopulation();
        void SetPopulation(unsigned int pop);
        unsigned int GetFunds();
        void SetFunds(unsigned int amount);

    private:
        unsigned int population;
        unsigned int funds;
};