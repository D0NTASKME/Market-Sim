#include "agents/market_maker.hpp"
#include "market.hpp"
#include <algorithm>
#include <cmath>

void MarketMaker::cancel_quotes(Market& market) {
    for (uint64_t id : live_quote_ids_) market.cancel(id);
    live_quote_ids_.clear();
}

double MarketMaker::reference_price(Market& market) { return market.mid_price(); }
double MarketMaker::half_spread(Market&) { return static_cast<double>(half_spread_); }

void MarketMaker::act(Market& market) {
    cancel_quotes(market);

    double ref = reference_price(market);
    double hs  = std::max(1.0, half_spread(market));   // never quote inside one tick
    int64_t inv = market.position(id_).inventory;

    int64_t bid = static_cast<int64_t>(std::floor(ref - hs));
    int64_t ask = static_cast<int64_t>(std::ceil(ref + hs));
    if (ask <= bid) ask = bid + 1;

    Order o;
    o.id = 0;
    o.quantity = quote_size_;
    if (inv < max_inventory_) {
        o.side = Side::Buy;  o.price_ticks = bid;
        live_quote_ids_.push_back(market.submit(o, id_));
    }
    if (inv > -max_inventory_) {
        o.side = Side::Sell; o.price_ticks = ask;
        live_quote_ids_.push_back(market.submit(o, id_));
    }
}

double SkewedMarketMaker::reference_price(Market& market) {
    return market.mid_price() - skew_per_unit_ * market.position(id_).inventory;
}

double AvellanedaStoikovMaker::time_left(Market& market) const {
    return std::max(0.0, horizon_ - market.time());
}

double AvellanedaStoikovMaker::reference_price(Market& market) {
    double q = static_cast<double>(market.position(id_).inventory);
    return market.mid_price() - q * gamma_ * sigma_ * sigma_ * time_left(market);
}

double AvellanedaStoikovMaker::half_spread(Market& market) {
    double total = gamma_ * sigma_ * sigma_ * time_left(market)
                 + (2.0 / gamma_) * std::log(1.0 + gamma_ / k_);
    return total / 2.0;
}
