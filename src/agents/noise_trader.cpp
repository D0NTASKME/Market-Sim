#include "agents/noise_trader.hpp"
#include "market.hpp"

#include <cmath>

void NoiseTrader::act(Market &market) {
  std::mt19937 &rng = market.rng();

  std::uniform_int_distribution<int> coin(0, 1);
  std::uniform_int_distribution<uint32_t> qty(1, max_qty_);
  std::uniform_int_distribution<int64_t> offset(0, price_spread_);
  std::uniform_real_distribution<double> aggression(0.0, 1.0);

  Side side = coin(rng) == 0 ? Side::Buy : Side::Sell;

  // Reference point. Early on the book may be empty, in which case
  // mid_price() falls back to the fundamental so we still have somewhere
  // sensible to quote around.
  int64_t mid = static_cast<int64_t>(std::llround(market.mid_price()));

  int64_t price;
  if (aggression(rng) < 0.2) {
    // One order in five is marketable: priced through the touch so it
    // trades immediately rather than resting. Without some of these the
    // book just fills up and nothing ever executes.
    price = (side == Side::Buy) ? mid + price_spread_ : mid - price_spread_;
  } else {
    // The rest rest: priced on their own side of the mid, so they sit in
    // the book and give others something to trade against.
    price = (side == Side::Buy) ? mid - 1 - offset(rng) : mid + 1 + offset(rng);
  }

  Order o;
  o.id = 0; // Market::submit assigns the real id
  o.side = side;
  o.price_ticks = price;
  o.quantity = qty(rng);

  market.submit(o, id_);
}
