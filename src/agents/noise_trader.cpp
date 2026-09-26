#include "agents/noise_trader.hpp"
#include "market.hpp"
#include <cmath>
#include <vector>

void NoiseTrader::act(Market& market) {
    std::mt19937& rng = market.rng();

    // Cancel anything that has outlived its lifetime. Cancelling an order that
    // has already been filled is a harmless no-op.
    if (mean_lifetime_ > 0.0) {
        std::vector<std::pair<double, uint64_t>> keep;
        for (const auto& [expiry, oid] : expiries_) {
            if (expiry <= market.time()) market.cancel(oid);
            else keep.push_back({expiry, oid});
        }
        expiries_.assign(keep.begin(), keep.end());
    }
    std::uniform_int_distribution<int>      coin(0, 1);
    std::uniform_int_distribution<uint32_t> qty(1, max_qty_);
    std::uniform_int_distribution<int64_t>  offset(0, price_spread_);
    std::uniform_real_distribution<double>  aggression(0.0, 1.0);

    Side side = coin(rng) == 0 ? Side::Buy : Side::Sell;
    int64_t mid = static_cast<int64_t>(std::llround(market.mid_price()));

    int64_t price;
    if (aggression(rng) < 0.2) {
        price = (side == Side::Buy) ? mid + price_spread_ : mid - price_spread_;
    } else {
        price = (side == Side::Buy) ? mid - 1 - offset(rng) : mid + 1 + offset(rng);
    }

    Order o;
    o.id = 0;
    o.side = side;
    o.price_ticks = price;
    o.quantity = qty(rng);
    uint64_t oid = market.submit(o, id_);
    if (mean_lifetime_ > 0.0) {
        std::exponential_distribution<double> life(1.0 / mean_lifetime_);
        expiries_.push_back({market.time() + life(rng), oid});
    }
}
