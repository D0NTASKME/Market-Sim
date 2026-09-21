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
    // The two decisions a maker makes: where to centre its quotes, and how
    // wide to make them. Subclasses override one or both.
    virtual double reference_price(Market& market);
    virtual double half_spread(Market& market);

    double rate_;
    int64_t half_spread_;
    uint32_t quote_size_;
    int64_t max_inventory_;

private:
    void cancel_quotes(Market& market);
    std::vector<uint64_t> live_quote_ids_;
};

// Hand-tuned inventory skew: shift quotes against the position.
class SkewedMarketMaker : public MarketMaker {
public:
    SkewedMarketMaker(uint64_t id, double rate, int64_t half_spread,
                      uint32_t quote_size, int64_t max_inventory, double skew_per_unit)
        : MarketMaker(id, rate, half_spread, quote_size, max_inventory),
          skew_per_unit_(skew_per_unit) {}
protected:
    double reference_price(Market& market) override;
private:
    double skew_per_unit_;
};

// Avellaneda & Stoikov (2008), "High-frequency trading in a limit order book".
//
//   reservation price  r = s - q * gamma * sigma^2 * (T - t)
//   total spread       d = gamma * sigma^2 * (T - t) + (2 / gamma) * ln(1 + gamma / k)
//
// s = mid, q = inventory, gamma = risk aversion, sigma = volatility,
// T - t = time remaining, k = how fast fill probability decays with distance.
class AvellanedaStoikovMaker : public MarketMaker {
public:
    AvellanedaStoikovMaker(uint64_t id, double rate, uint32_t quote_size,
                           int64_t max_inventory, double gamma, double sigma,
                           double k, double horizon)
        : MarketMaker(id, rate, 1, quote_size, max_inventory),
          gamma_(gamma), sigma_(sigma), k_(k), horizon_(horizon) {}
protected:
    double reference_price(Market& market) override;
    double half_spread(Market& market) override;
private:
    double time_left(Market& market) const;
    double gamma_, sigma_, k_, horizon_;
};
