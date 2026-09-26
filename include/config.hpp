#pragma once
// Every number the simulator uses, in one place, loadable from a plain text
// file of "key value" lines. Defaults reproduce the realistic market used in
// Phase 6 exactly, so existing results are unchanged.
//
// Two families of noise trader:
//   noise_model = legacy     the original uniform traders (Phases 1-6)
//   noise_model = calibrated order sizes, placement and lifetimes drawn from
//                            tables measured on real data by calibrate.py

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct SimConfig {
    // --- market ------------------------------------------------------------
    uint32_t seed = 1;
    double duration = 1800.0;
    double fundamental_start = 10100.0;
    double fundamental_vol = 2.0;        // ticks per sqrt(second)

    // --- noise traders -----------------------------------------------------
    std::string noise_model = "legacy";
    int noise_count = 10;

    // legacy model
    double noise_rate = 2.0;             // per trader
    int64_t noise_price_spread = 5;
    uint32_t noise_max_qty = 100;
    double noise_lifetime = 10.0;        // mean seconds; 0 = never cancel

    // calibrated model (rates are totals across all noise traders)
    double passive_rate = 0.0;           // resting limit orders per second
    double aggressive_rate = 0.0;        // immediate-or-cancel orders per second
    std::vector<double> size_quantiles;       // new limit order sizes
    std::vector<double> trade_size_quantiles; // aggressive order sizes
    std::vector<double> lifetime_quantiles;       // seconds until cancellation, all orders
    std::vector<double> lifetime_touch_quantiles; // if given, used for orders at or inside the touch
    std::vector<double> lifetime_near_quantiles;  //   ... 1-2 ticks behind the best
    std::vector<double> lifetime_deep_quantiles;  //   ... 3+ ticks behind
    std::vector<double> placement_probs;      // [0] = inside spread, [k+1] = k ticks behind best

    // --- reactive liquidity providers ---------------------------------------
    // Share of orders placed at the touch that are algorithmic quotes, measured
    // as the share cancelled within 100 ms. 0 disables them entirely.
    double reactive_share = 0.0;
    int reactive_count = 5;
    double reactive_wake_rate = 50.0;   // how often each one looks at the market
    double reactive_quote_life = 0.02;  // seconds before a quote is repriced (measured)

    // --- market maker -------------------------------------------------------
    std::string maker = "skewed";        // naive | skewed | as
    double maker_rate = 20.0;
    double maker_half_spread = 1.0;
    uint32_t maker_size = 50;
    int64_t maker_max_inventory = 2000;
    double maker_skew = 0.005;
    double as_gamma = 5e-6, as_k = 1.0;

    // --- informed trader ------------------------------------------------------
    std::string informed_model = "legacy"; // legacy | calibrated
    double informed_rate = 5.0;
    double informed_threshold = 3.0;
    uint32_t informed_size = 200;
    int64_t informed_cap = 1'000'000;

    static SimConfig load(const std::string& path);
    static SimConfig parse(std::istream& in, const std::string& where = "<config>");
    void save(const std::string& path) const;
};
