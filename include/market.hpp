#pragma once
#include "order_book.hpp"
#include "agent.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <deque>
#include <fstream>
#include <queue>
#include <set>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// Everything a market maker could legitimately observe at one instant, plus
// the target it can't see. Used to train a predictor of mispricing.
struct FeatureRow {
    double t;
    double ofi_1s, ofi_5s;      // signed aggressive volume: buy-initiated minus sell-initiated
    double trade_signs_10;      // sum of the signs of the last 10 trades
    double book_imbalance;      // (bid qty - ask qty) / total, at the touch
    double spread;
    double dmid_1s, dmid_5s;    // mid now minus mid 1s / 5s ago
    double maker_inventory;     // the maker's own position is observable to it
    double mid;
    double target;              // fundamental - mid: hidden, training label only
};

struct DepthLevel { int64_t price; int64_t qty; int64_t mine; };

struct Position { int64_t inventory = 0; double cash = 0.0; };
struct SimEvent {
    double time; size_t agent_index;
    bool operator>(const SimEvent& o) const { return time > o.time; }
};

class Market {
public:
    Market(uint32_t seed, double fundamental_start, double fundamental_vol);
    void add_agent(std::unique_ptr<Agent> a);
    void run(double duration);        // start and run to `duration` in one go
    void start();                     // seed the event queue; idempotent
    void run_until(double until);     // advance to `until`; safe to call repeatedly

    // Up to n price levels from the touch outward, with the maker's share.
    std::vector<DepthLevel> depth(Side side, size_t n) const;

    double time() const { return now_; }
    double fundamental() const { return fundamental_; }
    std::optional<int64_t> best_bid() const { return book_.best_bid(); }
    std::optional<int64_t> best_ask() const { return book_.best_ask(); }
    double mid_price() const;
    const Position& position(uint64_t agent_id) const;

    uint64_t submit(Order o, uint64_t agent_id);
    // Immediate-or-cancel: trades what it can at once; any remainder vanishes
    // without ever resting, so it appears in LOBSTER output only as executions.
    uint64_t submit_ioc(Order o, uint64_t agent_id);
    bool cancel(uint64_t order_id);
    std::mt19937& rng() { return rng_; }

    void track_agent(uint64_t agent_id) { tracked_ = agent_id; }

    // Attribution: label agents so trades can be bucketed by counterparty type.
    void set_maker(uint64_t id)    { maker_id_ = id; }
    void set_informed(uint64_t id) { informed_ids_.insert(id); }

    // Maker's realised cash flow split by who it traded against.
    double maker_flow_vs_informed() const { return maker_vs_informed_; }
    double maker_flow_vs_noise()    const { return maker_vs_noise_; }
    uint64_t informed_trades() const { return informed_trades_; }
    // Most recent trade, for agents that react to order flow.
    double last_trade_time() const { return last_trade_time_; }
    int last_trade_sign() const { return last_trade_sign_; }   // +1 buyer-initiated, -1 seller
    int64_t maker_qty_vs_informed() const { return qty_vs_informed_; }
    int64_t maker_qty_vs_noise()    const { return qty_vs_noise_; }
    // Fixed-interval price series, for return statistics. Event-count
    // sampling is no good here: returns have to be over equal time steps or
    // the variance is meaningless.
    void record_prices_every(double dt) { price_dt_ = dt; next_price_t_ = dt; }
    const std::vector<double>& price_series() const { return price_series_; }
    void dump_prices(const std::string& path) const;

    // Write every book event in LOBSTER format (message + order book files),
    // so the same analysis runs on simulated and real NASDAQ data. Prices are
    // written as ticks x 100 so one tick is 100 units, like a one-cent tick.
    void enable_lobster_log(const std::string& prefix, int levels = 10);

    // Sample observable features at a fixed interval.
    void record_features_every(double dt) { feature_dt_ = dt; next_feature_t_ = dt; }
    const std::vector<FeatureRow>& features() const { return features_; }

    double mean_abs_inventory() const { return inv_samples_ ? inv_abs_sum_ / inv_samples_ : 0.0; }
    void dump_csv(const std::string& path) const;

private:
    void on_trade(uint64_t resting_id, uint64_t incoming_id, int64_t price, uint32_t qty);
    void step_fundamental(double dt);

    OrderBook book_;
    double now_ = 0.0;
    double fundamental_;
    double fundamental_vol_;

    std::vector<std::unique_ptr<Agent>> agents_;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<>> queue_;

    std::unordered_map<uint64_t, Position> positions_;
    struct OrderInfo { uint64_t agent_id; Side side; };
    std::unordered_map<uint64_t, OrderInfo> order_owner_;
    uint64_t next_order_id_ = 1;
    uint64_t tracked_ = 0;
    uint64_t informed_trades_ = 0;
    uint64_t last_informed_order_ = 0;
    double last_trade_time_ = -1.0;
    int last_trade_sign_ = 0;
    bool started_ = false;
    uint64_t maker_id_ = 0;
    std::set<uint64_t> informed_ids_;
    double maker_vs_informed_ = 0.0;
    double maker_vs_noise_ = 0.0;
    int64_t qty_vs_informed_ = 0;
    int64_t qty_vs_noise_ = 0;
    double inv_abs_sum_ = 0.0;
    double price_dt_ = 0.0;
    double feature_dt_ = 0.0;
    double next_feature_t_ = 0.0;
    std::vector<FeatureRow> features_;
    std::deque<std::pair<double, double>> recent_trades_;   // (time, signed qty)
    std::deque<int> recent_signs_;
    std::deque<std::pair<double, double>> recent_mids_;     // (time, mid)
    FeatureRow snapshot_features();

    // LOBSTER logging. adjust_* lets an execution report the book as it will
    // be AFTER the fill, since the trade callback fires before the book updates.
    std::unique_ptr<std::ofstream> lob_msg_, lob_book_;
    int lob_levels_ = 10;
    void lob_log(int type, uint64_t id, uint32_t size, int64_t price, Side side,
                 Side adjust_side = Side::Buy, int64_t adjust_price = 0, uint32_t adjust_qty = 0);
    double next_price_t_ = 0.0;
    std::vector<double> price_series_;
    size_t inv_samples_ = 0;

    std::mt19937 rng_;
    std::vector<std::array<double, 5>> log_;
    size_t event_count_ = 0;
};
