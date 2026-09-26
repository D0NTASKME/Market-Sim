#include "agents/calibrated.hpp"
#include "market.hpp"

#include <algorithm>
#include <cmath>

namespace {
uint32_t to_size(double x) { return static_cast<uint32_t>(std::max(1.0, std::round(x))); }
}

void CalibratedTrader::act(Market& market) {
    std::mt19937& rng = market.rng();
    now_ = market.time();

    // Cancel orders whose lifetime has run out. Cancelling one that has already
    // been filled is a harmless no-op.
    while (!expiries_.empty() && expiries_.top().first <= now_ + 1e-9) {
        market.cancel(expiries_.top().second);
        expiries_.pop();
    }

    // This wake-up may have been for an expiry only. Compare with a tolerance:
    // the event time is now_ + (next_order_ - now_), which floating point
    // doesn't always return as exactly next_order_.
    if (next_order_ > now_ + 1e-9) return;
    next_order_ = -1.0;

    std::uniform_real_distribution<double> u(0.0, 1.0);
    const Side side = u(rng) < 0.5 ? Side::Buy : Side::Sell;
    const auto bb = market.best_bid(), ba = market.best_ask();

    if (u(rng) < aggressive_share_) {
        // Take liquidity at the opposite touch; any unfilled part is dropped.
        auto touch = side == Side::Buy ? ba : bb;
        if (!touch) return;
        market.submit_ioc(Order{0, side, *touch, to_size(trade_sizes_.sample(rng))}, id_);
        return;
    }

    // Resting order: k ticks behind the best price on its own side, or one
    // tick inside the spread if there is room to improve.
    const int bucket = placement_(rng);
    int64_t price;
    const int64_t mid = static_cast<int64_t>(std::llround(market.mid_price()));
    if (side == Side::Buy) {
        int64_t best = bb ? *bb : (ba ? *ba - 1 : mid - 1);
        if (bucket == 0) price = (ba && *ba - best > 1) ? best + 1 : best;
        else             price = best - (bucket - 1);
    } else {
        int64_t best = ba ? *ba : (bb ? *bb + 1 : mid + 1);
        if (bucket == 0) price = (bb && best - *bb > 1) ? best - 1 : best;
        else             price = best + (bucket - 1);
    }
    uint64_t oid = market.submit(Order{0, side, price, to_size(sizes_.sample(rng))}, id_);
    const Quantiles& life = bucket <= 1 ? life_touch_ : (bucket <= 3 ? life_near_ : life_deep_);
    if (!life.empty()) expiries_.push({now_ + life.sample(rng), oid});
}

void CalibratedInformed::act(Market& market) {
    const double fair = market.fundamental(), mid = market.mid_price();
    if (std::abs(fair - mid) < threshold_) return;
    const int64_t inv = market.position(id_).inventory;
    std::mt19937& rng = market.rng();
    if (fair > mid) {
        if (inv >= cap_) return;
        if (auto a = market.best_ask())
            market.submit_ioc(Order{0, Side::Buy, *a, to_size(trade_sizes_.sample(rng))}, id_);
    } else {
        if (inv <= -cap_) return;
        if (auto b = market.best_bid())
            market.submit_ioc(Order{0, Side::Sell, *b, to_size(trade_sizes_.sample(rng))}, id_);
    }
}
