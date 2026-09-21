#pragma once
#include "order_book.hpp"
#include "agent.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <queue>
#include <set>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct Position { int64_t inventory = 0; double cash = 0.0; };
struct SimEvent {
    double time; size_t agent_index;
    bool operator>(const SimEvent& o) const { return time > o.time; }
};

class Market {
public:
    Market(uint32_t seed, double fundamental_start, double fundamental_vol);
    void add_agent(std::unique_ptr<Agent> a);
    void run(double duration);

    double time() const { return now_; }
    double fundamental() const { return fundamental_; }
    std::optional<int64_t> best_bid() const { return book_.best_bid(); }
    std::optional<int64_t> best_ask() const { return book_.best_ask(); }
    double mid_price() const;
    const Position& position(uint64_t agent_id) const;

    uint64_t submit(Order o, uint64_t agent_id);
    bool cancel(uint64_t order_id);
    std::mt19937& rng() { return rng_; }

    void track_agent(uint64_t agent_id) { tracked_ = agent_id; }

    // Attribution: label agents so trades can be bucketed by counterparty type.
    void set_maker(uint64_t id)    { maker_id_ = id; }
    void set_informed(uint64_t id) { informed_ids_.insert(id); }

    // Maker's realised cash flow split by who it traded against.
    double maker_flow_vs_informed() const { return maker_vs_informed_; }
    double maker_flow_vs_noise()    const { return maker_vs_noise_; }
    int64_t maker_qty_vs_informed() const { return qty_vs_informed_; }
    int64_t maker_qty_vs_noise()    const { return qty_vs_noise_; }
    // Fixed-interval price series, for return statistics. Event-count
    // sampling is no good here: returns have to be over equal time steps or
    // the variance is meaningless.
    void record_prices_every(double dt) { price_dt_ = dt; next_price_t_ = dt; }
    const std::vector<double>& price_series() const { return price_series_; }
    void dump_prices(const std::string& path) const;

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
    uint64_t maker_id_ = 0;
    std::set<uint64_t> informed_ids_;
    double maker_vs_informed_ = 0.0;
    double maker_vs_noise_ = 0.0;
    int64_t qty_vs_informed_ = 0;
    int64_t qty_vs_noise_ = 0;
    double inv_abs_sum_ = 0.0;
    double price_dt_ = 0.0;
    double next_price_t_ = 0.0;
    std::vector<double> price_series_;
    size_t inv_samples_ = 0;

    std::mt19937 rng_;
    std::vector<std::array<double, 5>> log_;
    size_t event_count_ = 0;
};
