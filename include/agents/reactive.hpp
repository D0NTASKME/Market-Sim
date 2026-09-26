#pragma once
// Reactive liquidity provider: an algorithmic quoter that watches order flow.
//
// Real books show a large share of orders at the best price cancelled within
// 100 ms. Those aren't traders changing their minds; they're quotes being
// repriced. This agent reproduces that behaviour:
//
//   * when a trade goes through, it pulls its quotes on the side that was hit,
//     so the queue there thins and the price can move at once
//   * it cancels any quote that is no longer at the best price, so its
//     liquidity follows the touch
//   * otherwise it maintains a quote at the touch
//
// The fraction of touch liquidity that behaves this way is measured from the
// data (share of touch orders cancelled inside 100 ms), not tuned.

#include "../agent.hpp"
#include "calibrated.hpp"
#include "../order_book.hpp"

#include <cstdint>
#include <random>
#include <vector>

class ReactiveTrader : public Agent {
public:
    // wake_rate is how often it looks at the market; submit_rate is how often
    // it posts. Waking faster than it posts is what gives it a short reaction
    // time without changing the order arrival rate the data pinned down.
    // quote_life is how long a quote survives before being repriced, taken
    // from the measured median lifetime of orders at the touch. Without it the
    // quotes pile up, because they are only cancelled when hit or when the
    // price moves away, and in a one-tick market that is rare.
    ReactiveTrader(uint64_t id, double wake_rate, double submit_rate,
                   double quote_life, Quantiles sizes)
        : Agent(id), wake_rate_(wake_rate),
          submit_prob_(wake_rate > 0 ? submit_rate / wake_rate : 0.0),
          quote_life_(quote_life), sizes_(std::move(sizes)) {}

    void act(Market& market) override;
    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(wake_rate_);
        return d(rng);
    }

private:
    double wake_rate_, submit_prob_, quote_life_;
    Quantiles sizes_;
    double last_seen_trade_ = -1.0;
    struct Quote { uint64_t id; Side side; int64_t price; double expiry; };
    std::vector<Quote> quotes_;
};
