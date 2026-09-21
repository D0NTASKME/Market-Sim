#include "market.hpp"
#include "agents/noise_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/informed_trader.hpp"

#include <cstdio>
#include <memory>
#include <cmath>

void sweep(double informed_rate, int num_seeds = 30) {
    double sum_informed = 0, sum_noise = 0, sum_total = 0;
    double sum_informed_sq = 0;
    long long sum_qty_i = 0;

    for (int s = 1; s <= num_seeds; ++s) {
        Market market(s, 10100.0, 2.0);
        for (uint64_t i = 1; i <= 10; ++i)
            market.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100));
        market.add_agent(std::make_unique<MarketMaker>(100, 20.0, 1, 50, 2000));
        market.set_maker(100);
        if (informed_rate > 0.0) {
            market.add_agent(std::make_unique<InformedTrader>(200, informed_rate, 3.0, 200, 5000));
            market.set_informed(200);
        }
        market.run(300.0);

        const Position& p = market.position(100);
        double vi = market.maker_flow_vs_informed();
        sum_total    += p.cash + p.inventory * market.fundamental();
        sum_noise    += market.maker_flow_vs_noise();
        sum_informed += vi;
        sum_informed_sq += vi * vi;
        sum_qty_i    += market.maker_qty_vs_informed();
    }

    double n = num_seeds;
    double mean_i = sum_informed / n;
    double se_i = std::sqrt((sum_informed_sq / n - mean_i * mean_i) / n);

    std::printf("%6.1f  %10.0f  %10.0f  %10.0f +/- %-8.0f %8lld\n",
                informed_rate, sum_total / n, sum_noise / n,
                mean_i, 1.96 * se_i, (long long)(sum_qty_i / num_seeds));
}

int main() {
    std::printf("  rate   total P&L   vs noise      vs informed   qty(n)    qty(i)\n");
    for (double r : {0.0, 1.0, 2.0, 5.0, 10.0, 20.0}) sweep(r);
    return 0;
}
