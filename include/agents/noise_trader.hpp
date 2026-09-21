#pragma once

#include "agent.hpp"
#include <random>

class NoiseTrader : public Agent {
public:
    NoiseTrader(uint64_t id, double rate, int64_t price_spread, uint32_t max_qty)
        : Agent(id), rate_(rate), price_spread_(price_spread), max_qty_(max_qty) {}

    void act(Market& market) override;

    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(rate_);
        return d(rng);
    }

private:
    double rate_;            // actions per second
    int64_t price_spread_;   // how far from mid it places orders
    uint32_t max_qty_;
};
