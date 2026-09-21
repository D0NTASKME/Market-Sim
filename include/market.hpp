#pragma once

#include "agent.hpp"
#include "order_book.hpp"

#include <cstdint>
#include <memory>
#include <queue>
#include <random>
#include <unordered_map>
#include <vector>
#include <optional>

#include <queue>
#include <random>
#include <set>
#include <string>
struct Position {
  int64_t inventory = 0; // units held, signed
  double cash = 0.0;
};

struct SimEvent {
  double time;
  size_t agent_index;
  bool operator>(const SimEvent &o) const { return time > o.time; }
};
struct OrderInfo { uint64_t agent_id; Side side; };


class Market {
public:
    Market(uint32_t seed, double fundamental_start, double fundamental_vol);

    void add_agent(std::unique_ptr<Agent> a);
    void run(double duration);
    void track_agent(uint64_t id) { tracked_ = id; }
    void set_maker(uint64_t id)    { maker_id_ = id; }
    void set_informed(uint64_t id) { informed_ids_.insert(id); }

    double  maker_flow_vs_informed() const { return maker_vs_informed_; }
    double  maker_flow_vs_noise()    const { return maker_vs_noise_; }
    int64_t maker_qty_vs_informed()  const { return qty_vs_informed_; }
    int64_t maker_qty_vs_noise()     const { return qty_vs_noise_; }

    // --- what agents can see ---
    double time() const { return now_; }
    double fundamental() const { return fundamental_; }
    std::optional<int64_t> best_bid() const { return book_.best_bid(); }
    std::optional<int64_t> best_ask() const { return book_.best_ask(); }
    double mid_price() const; // falls back to fundamental if a side is empty
    const Position &position(uint64_t agent_id) const;

    // --- what agents can do ---
    uint64_t submit(Order o, uint64_t agent_id); // returns the order id
    bool cancel(uint64_t order_id);

    std::mt19937 &rng() { return rng_; }

    // --- recording ---
    void dump_csv(const std::string &path) const;

private:
    void on_trade(uint64_t buyer_id, uint64_t seller_id, int64_t price,
                uint32_t qty);
    void step_fundamental(double dt);

    OrderBook book_;
    double now_ = 0.0;
    double fundamental_;
    double fundamental_vol_;
    uint64_t maker_id_ = 0;
    std::set<uint64_t> informed_ids_;
    double  maker_vs_informed_ = 0.0;
    double  maker_vs_noise_    = 0.0;
    int64_t qty_vs_informed_   = 0;
    int64_t qty_vs_noise_      = 0;

    std::vector<std::unique_ptr<Agent>> agents_;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<>> queue_;

    std::unordered_map<uint64_t, Position> positions_;
    std::unordered_map<uint64_t, OrderInfo> order_owner_;
    uint64_t next_order_id_ = 1;
    uint64_t tracked_ = 0;
    size_t event_count_ = 0;

    std::mt19937 rng_;
    std::vector<std::array<double, 5>>
        log_; // time, mid, fundamental, mm inventory
};
