#include "agents/informed_trader.hpp"
#include "market.hpp"
#include <cmath>

void InformedTrader::act(Market& market) {
    double fair = market.fundamental();
    double mid  = market.mid_price();

    if (std::abs(fair - mid) < threshold_) return;

    int64_t inv = market.position(id_).inventory;

    Order o;
    o.id = 0;
    o.quantity = qty_;

    if (fair > mid) {
        if (inv >= max_position_) return;   // already as long as we allow
        o.side = Side::Buy;
        o.price_ticks = static_cast<int64_t>(std::llround(fair)) + 10;
    } else {
        if (inv <= -max_position_) return;
        o.side = Side::Sell;
        o.price_ticks = static_cast<int64_t>(std::llround(fair)) - 10;
    }

    market.submit(o, id_);
}
