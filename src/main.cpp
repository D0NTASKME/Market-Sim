#include "build.hpp"
#include "config.hpp"
#include "market.hpp"
#include "agents/noise_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/informed_trader.hpp"

#include <cmath>
#include <fstream>
#include <string>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

using MakerFactory = std::function<std::unique_ptr<Agent>()>;

struct Result { double total, total_ci, vs_informed, inv_abs; };

Result evaluate(const MakerFactory& make, double informed_rate, int seeds = 30) {
    std::vector<double> totals;
    double sum_vi = 0, sum_inv = 0;
    for (int s = 1; s <= seeds; ++s) {
        Market market(s, 10100.0, 2.0);
        for (uint64_t i = 1; i <= 10; ++i)
            market.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100));
        market.add_agent(make());
        market.set_maker(100);
        market.track_agent(100);
        if (informed_rate > 0.0) {
            market.add_agent(std::make_unique<InformedTrader>(200, informed_rate, 3.0, 200, 5000));
            market.set_informed(200);
        }
        market.run(300.0);
        const Position& p = market.position(100);
        totals.push_back(p.cash + p.inventory * market.fundamental());
        sum_vi += market.maker_flow_vs_informed();
        sum_inv += std::abs(static_cast<double>(market.mean_abs_inventory()));
    }
    double n = seeds, m = 0; for (double t : totals) m += t; m /= n;
    double v = 0; for (double t : totals) v += (t - m) * (t - m); v /= (n - 1);
    return {m, 1.96 * std::sqrt(v / n), sum_vi / n, sum_inv / n};
}

// --- Experiment 3: stylised facts ------------------------------------------
// Pools one-second returns over many independent 300s runs. Short runs, not
// one long one: over long horizons the informed trader hits its position cap
// and price discovery breaks down, which would contaminate the statistics.
void stylised_facts(double informed_rate, const std::string& path, int seeds = 80) {
    std::ofstream out(path);
    out << "seed,r\n";
    for (int s = 1; s <= seeds; ++s) {
        Market market(s, 10100.0, 2.0);
        for (uint64_t i = 1; i <= 10; ++i)
            market.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100));
        market.add_agent(std::make_unique<SkewedMarketMaker>(100, 20.0, 1, 50, 2000, 0.005));
        if (informed_rate > 0.0)
            market.add_agent(std::make_unique<InformedTrader>(200, informed_rate, 3.0, 200, 5000));
        market.record_prices_every(1.0);
        market.run(300.0);

        const auto& p = market.price_series();
        for (size_t i = 11; i < p.size(); ++i)   // skip warm-up: the book starts empty
            out << s << ',' << (p[i] - p[i - 1]) << '\n';
    }
    std::printf("wrote %s\n", path.c_str());
}

void compare_makers() {
    std::vector<std::pair<const char*, MakerFactory>> makers = {
        {"naive ", [] { return std::make_unique<MarketMaker>(100, 20.0, 1, 50, 2000); }},
        {"skewed", [] { return std::make_unique<SkewedMarketMaker>(100, 20.0, 1, 50, 2000, 0.005); }},
        {"A-S   ", [] { return std::make_unique<AvellanedaStoikovMaker>(100, 20.0, 50, 2000, 5e-6, 2.0, 1.0, 300.0); }},
    };
    std::printf("rate  maker     total P&L (95%% CI)      vs informed   mean |inv|\n");
    for (double r : {0.0, 5.0, 10.0, 20.0}) {
        for (auto& [name, f] : makers) {
            Result res = evaluate(f, r);
            std::printf("%4.0f  %s  %9.0f +/- %-8.0f  %10.0f  %9.0f\n",
                        r, name, res.total, res.total_ci, res.vs_informed, res.inv_abs);
        }
        std::printf("\n");
    }
}

void gamma_sweep() {
    std::printf("gamma      total P&L (95%% CI)     vs informed   mean |inv|\n");
    for (double g : {1e-6, 5e-6, 2e-5, 1e-4, 5e-4}) {
        auto f = [g] { return std::make_unique<AvellanedaStoikovMaker>(100, 20.0, 50, 2000, g, 2.0, 1.0, 300.0); };
        Result res = evaluate(f, 10.0);
        std::printf("%-8.0e  %9.0f +/- %-8.0f  %10.0f  %9.1f\n",
                    g, res.total, res.total_ci, res.vs_informed, res.inv_abs);
    }
}

// --- Experiment 4: what can a market maker learn from order flow? ---------
// Samples observable features and the hidden mispricing every half second.
// Seed and informed rate are written with each row so the analysis can split
// train and test by seed, never within a single run.
void dump_features(const std::string& path, double lifetime = 0.0, int64_t cap = 5000, int seeds = 40) {
    std::ofstream out(path);
    out << "seed,rate,t,ofi_1s,ofi_5s,trade_signs_10,book_imbalance,spread,"
           "dmid_1s,dmid_5s,maker_inventory,mid,target\n";
    for (double rate : {0.0, 2.0, 5.0, 10.0, 20.0}) {
        for (int s = 1; s <= seeds; ++s) {
            Market market(s, 10100.0, 2.0);
            for (uint64_t i = 1; i <= 10; ++i)
                market.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100, lifetime));
            market.add_agent(std::make_unique<SkewedMarketMaker>(100, 20.0, 1, 50, 2000, 0.005));
            market.set_maker(100);
            if (rate > 0.0) {
                market.add_agent(std::make_unique<InformedTrader>(200, rate, 3.0, 200, cap));
                market.set_informed(200);
            }
            market.record_features_every(0.5);
            market.run(300.0);
            for (const FeatureRow& f : market.features()) {
                if (f.t < 10.0) continue;   // skip warm-up: the book starts empty
                out << s << ',' << rate << ',' << f.t << ',' << f.ofi_1s << ',' << f.ofi_5s << ','
                    << f.trade_signs_10 << ',' << f.book_imbalance << ',' << f.spread << ','
                    << f.dmid_1s << ',' << f.dmid_5s << ',' << f.maker_inventory << ',' << f.mid << ',' << f.target << '\n';
            }
        }
    }
    std::printf("wrote %s\n", path.c_str());
}

// --- Experiment 5: write the simulator's output in LOBSTER format ----------
// Realistic market (10s mean order lifetime), one long session, so the same
// analysis script can be run on this and on real NASDAQ data.
void export_lobster(const std::string& prefix, double duration = 1800.0) {
    Market market(1, 10100.0, 2.0);
    for (uint64_t i = 1; i <= 10; ++i)
        market.add_agent(std::make_unique<NoiseTrader>(i, 2.0, 5, 100, 10.0));
    market.add_agent(std::make_unique<SkewedMarketMaker>(100, 20.0, 1, 50, 2000, 0.005));
    market.set_maker(100);
    market.add_agent(std::make_unique<InformedTrader>(200, 5.0, 3.0, 200, 1'000'000));
    market.set_informed(200);
    market.enable_lobster_log(prefix, 10);
    market.run(duration);
    std::printf("wrote %s_{message,orderbook}_10.csv (%.0f simulated seconds)\n", prefix.c_str(), duration);
}

// --- Configurable run ---------------------------------------------------
// ./sim run CONFIG [LOBSTER_PREFIX]   run one market from a config file,
//                                     optionally writing LOBSTER output
int run_config(const std::string& cfg_path, const std::string& prefix) {
    SimConfig c = SimConfig::load(cfg_path);
    auto m = build_market(c);
    if (!prefix.empty()) m->enable_lobster_log(prefix, 10);
    m->run(c.duration);
    const Position& p = m->position(MAKER_ID);
    std::printf("ran %s for %.0f s | maker P&L %.0f, inventory %lld | vs noise %.0f, vs informed %.0f"
                " | informed_trades_per_s %.5f | mid-fair gap %.2f\n",
                cfg_path.c_str(), c.duration, p.cash + p.inventory * m->fundamental(),
                (long long)p.inventory, m->maker_flow_vs_noise(), m->maker_flow_vs_informed(),
                m->informed_trades() / c.duration, m->mid_price() - m->fundamental());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "run")
        return run_config(argv[2], argc > 3 ? argv[3] : "");
    std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "makers" || mode == "all") compare_makers();
    if (mode == "gamma"  || mode == "all") gamma_sweep();
    if (mode == "features") dump_features("results/features.csv");
    if (mode == "lobster") export_lobster("results/SIM_2012-06-21_34200000_36000000");
    if (mode == "features_v2") dump_features("results/features_v2.csv", 10.0, 1'000'000);
    if (mode == "facts"  || mode == "all") {
        stylised_facts(0.0, "results/returns_0.csv");
        stylised_facts(5.0, "results/returns_5.csv");
    }
    return 0;
}
