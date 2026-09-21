#pragma once
#include "../agent.hpp"
#include <cstdint>
#include <random>
#include <vector>

class MarketMaker : public Agent {
public:
    MarketMaker(uint64_t id, double rate, int64_t half_spread,
                uint32_t quote_size, int64_t max_inventory)
        : Agent(id), rate_(rate), half_spread_(half_spread),
          quote_size_(quote_size), max_inventory_(max_inventory) {}

    void act(Market& market) override;

    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(rate_);
        return d(rng);
    }

protected:
    // Overridden by the Avellaneda-Stoikov variant in phase 4. The naive
    // maker just quotes around the mid; a smarter one shifts this.
    virtual double reference_price(Market& market);

    double rate_;
    int64_t half_spread_;
    uint32_t quote_size_;
    int64_t max_inventory_;

private:
    void cancel_quotes(Market& market);
    std::vector<uint64_t> live_quote_ids_;
};
