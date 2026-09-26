#pragma once
#include "../agent.hpp"
#include <cstdint>
#include <deque>
#include <random>
#include <utility>
class NoiseTrader : public Agent {
public:
    // mean_lifetime: resting orders are cancelled after an exponentially
    // distributed lifetime with this mean, in seconds. 0 means never cancel,
    // which was the original behaviour.
    NoiseTrader(uint64_t id, double rate, int64_t price_spread, uint32_t max_qty,
                double mean_lifetime = 0.0)
        : Agent(id), rate_(rate), price_spread_(price_spread), max_qty_(max_qty),
          mean_lifetime_(mean_lifetime) {}
    void act(Market& market) override;
    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(rate_);
        return d(rng);
    }
private:
    double rate_;
    int64_t price_spread_;
    uint32_t max_qty_;
    double mean_lifetime_;
    std::deque<std::pair<double, uint64_t>> expiries_;   // (expiry time, order id)
};
