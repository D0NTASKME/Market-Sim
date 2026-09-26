#pragma once
// Agents whose behaviour is drawn from distributions measured on real order
// book data (see analysis/calibrate.py). Kept separate from the original
// agents so every earlier experiment stays exactly reproducible.

#include "../agent.hpp"
#include <cstdint>
#include <algorithm>
#include <functional>
#include <queue>
#include <random>
#include <utility>
#include <vector>

// Samples from an empirical distribution given as evenly spaced quantiles
// (q[0] = minimum, q.back() = maximum), interpolating linearly between them.
// Non-parametric on purpose: real order sizes are lumpy (round lots of 100)
// and heavy-tailed in ways a lognormal fit would smooth away.
class Quantiles {
public:
    Quantiles() = default;
    explicit Quantiles(std::vector<double> q) : q_(std::move(q)) {}
    bool empty() const { return q_.size() < 2; }
    double sample(std::mt19937& rng) const {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        double x = u(rng) * static_cast<double>(q_.size() - 1);
        size_t i = static_cast<size_t>(x);
        if (i >= q_.size() - 1) return q_.back();
        double f = x - static_cast<double>(i);
        return q_[i] + f * (q_[i + 1] - q_[i]);
    }
private:
    std::vector<double> q_;
};

// Uninformed trader. Each action is either a resting limit order, placed
// relative to the best price on its own side, or an immediate-or-cancel order
// that takes liquidity at the touch.
class CalibratedTrader : public Agent {
public:
    // Lifetimes are drawn by where the order was placed, because in real
    // books how long an order lives depends on how close it is to the touch:
    //   life_touch: inside the spread or at the best price
    //   life_near:  1-2 ticks behind the best
    //   life_deep:  3 or more ticks behind
    CalibratedTrader(uint64_t id, double rate, double aggressive_share,
                     Quantiles sizes, Quantiles trade_sizes,
                     Quantiles life_touch, Quantiles life_near, Quantiles life_deep,
                     std::vector<double> placement_probs)
        : Agent(id), rate_(rate), aggressive_share_(aggressive_share),
          sizes_(std::move(sizes)), trade_sizes_(std::move(trade_sizes)),
          life_touch_(std::move(life_touch)), life_near_(std::move(life_near)),
          life_deep_(std::move(life_deep)),
          placement_(placement_probs.begin(), placement_probs.end()) {}

    void act(Market& market) override;

    // Wakes for whichever comes first: its next order (a Poisson process at
    // `rate`) or the expiry of one of its resting orders. Waking for expiries
    // cancels orders exactly on time; checking only when the next order is
    // due would make every lifetime run long by up to one inter-order gap.
    double next_delay(std::mt19937& rng) override {
        if (next_order_ < 0.0) {
            std::exponential_distribution<double> d(rate_);
            next_order_ = now_ + d(rng);
        }
        double t = next_order_;
        if (!expiries_.empty()) t = std::min(t, expiries_.top().first);
        return std::max(0.0, t - now_);
    }

private:
    double rate_, aggressive_share_;
    Quantiles sizes_, trade_sizes_, life_touch_, life_near_, life_deep_;
    double now_ = 0.0;
    double next_order_ = -1.0;   // absolute time of the next order; < 0 means not yet drawn
    std::discrete_distribution<int> placement_;
    // (expiry time, order id), earliest first
    std::priority_queue<std::pair<double, uint64_t>,
                        std::vector<std::pair<double, uint64_t>>,
                        std::greater<>> expiries_;
};

// Informed trader for the calibrated market: sees the fundamental, and when
// the mid is at least `threshold` ticks wrong, takes liquidity at the touch
// with an immediate-or-cancel order sized like a real aggressive order.
class CalibratedInformed : public Agent {
public:
    CalibratedInformed(uint64_t id, double rate, double threshold,
                       Quantiles trade_sizes, int64_t cap)
        : Agent(id), rate_(rate), threshold_(threshold),
          trade_sizes_(std::move(trade_sizes)), cap_(cap) {}
    void act(Market& market) override;
    double next_delay(std::mt19937& rng) override {
        std::exponential_distribution<double> d(rate_);
        return d(rng);
    }
private:
    double rate_, threshold_;
    Quantiles trade_sizes_;
    int64_t cap_;
};
