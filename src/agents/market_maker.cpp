#include "agents/market_maker.hpp"
#include "market.hpp"
#include <cmath>

void MarketMaker::cancel_quotes(Market& market) {
    for (uint64_t id : live_quote_ids_) market.cancel(id);
    live_quote_ids_.clear();
}

double MarketMaker::reference_price(Market& market) {
    return market.mid_price();
}

void MarketMaker::act(Market& market) {
    cancel_quotes(market);
    int64_t inv = market.position(id_).inventory;
    int64_t skew = inv / 200;   // shift quotes down when long, up when short
    int64_t ref = static_cast<int64_t>(std::llround(reference_price(market))) - skew;


    Order o;
    o.id = 0;
    o.quantity = quote_size_;

    if (inv < max_inventory_) {
        o.side = Side::Buy;
        o.price_ticks = ref - half_spread_;
        live_quote_ids_.push_back(market.submit(o, id_));
    }
    if (inv > -max_inventory_) {
        o.side = Side::Sell;
        o.price_ticks = ref + half_spread_;
        live_quote_ids_.push_back(market.submit(o, id_));
    }
}
