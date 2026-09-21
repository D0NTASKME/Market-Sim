#pragma once

#include "agent.hpp"
#include <random>

class InformedTrader : public Agent {
public:
    InformedTrader(uint64_t id, double rate, double threshold,
                   uint32_t qty, int64_t max_position)
        : Agent(id), rate_(rate), threshold_(threshold),
          qty_(qty), max_position_(max_position) {}

    void act(Market& market) override;

    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(rate_);
        return d(rng);
    }

private:
    double rate_;
    double threshold_;
    uint32_t qty_;
    int64_t max_position_;
};
