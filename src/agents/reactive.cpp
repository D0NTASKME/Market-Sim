#include "agents/reactive.hpp"
#include "market.hpp"

#include <algorithm>
#include <cmath>

void ReactiveTrader::act(Market& market) {
    std::mt19937& rng = market.rng();
    const auto bb = market.best_bid(), ba = market.best_ask();

    // Did a trade happen since we last looked? If so, pull our quotes on the
    // side that was hit: a buyer-initiated trade (sign +1) took offers, so we
    // withdraw ours rather than sell more at a price that is about to rise.
    int faded = 0;
    if (market.last_trade_time() > last_seen_trade_) {
        faded = market.last_trade_sign();
        last_seen_trade_ = market.last_trade_time();
    }

    std::vector<Quote> kept;
    for (const Quote& qt : quotes_) {
        const bool hit_side = (faded > 0 && qt.side == Side::Sell) || (faded < 0 && qt.side == Side::Buy);
        // Drop quotes that are no longer at the best price: liquidity follows
        // the touch. And reprice on expiry, which is the churn that gives these
        // orders their very short lifetimes and keeps the touch thin.
        const bool stale = (qt.side == Side::Buy) ? (!bb || qt.price != *bb)
                                                  : (!ba || qt.price != *ba);
        if (hit_side || stale || qt.expiry <= market.time()) market.cancel(qt.id);
        else kept.push_back(qt);
    }
    quotes_.swap(kept);

    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (u(rng) >= submit_prob_) return;

    const Side side = u(rng) < 0.5 ? Side::Buy : Side::Sell;
    const int64_t mid = static_cast<int64_t>(std::llround(market.mid_price()));
    int64_t price;
    if (side == Side::Buy) price = bb ? *bb : (ba ? *ba - 1 : mid - 1);
    else                   price = ba ? *ba : (bb ? *bb + 1 : mid + 1);

    uint32_t size = static_cast<uint32_t>(std::max(1.0, std::round(sizes_.sample(rng))));
    std::exponential_distribution<double> life(1.0 / std::max(1e-6, quote_life_));
    quotes_.push_back({market.submit(Order{0, side, price, size}, id_), side, price,
                       market.time() + life(rng)});
}
