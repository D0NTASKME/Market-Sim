#include "market.hpp"

#include <cmath>
#include <fstream>

Market::Market(uint32_t seed, double fundamental_start, double fundamental_vol)
    : fundamental_(fundamental_start),
      fundamental_vol_(fundamental_vol),
      rng_(seed) {
    // The book knows nothing about agents or money. When a trade happens it
    // calls back here with the two order ids, and we turn that into position
    // changes for whoever owns those orders.
    book_.set_trade_callback(
        [this](uint64_t resting_id, uint64_t incoming_id, int64_t price, uint32_t qty) {
            on_trade(resting_id, incoming_id, price, qty);
        });
}

void Market::add_agent(std::unique_ptr<Agent> a) {
    positions_[a->id()] = Position{};   // start flat, zero cash
    agents_.push_back(std::move(a));
}

void Market::run(double duration) {
    // Seed: every agent gets a first action time drawn from its own
    // distribution, so they don't all fire at t = 0.
    for (size_t i = 0; i < agents_.size(); ++i) {
        queue_.push(SimEvent{agents_[i]->next_delay(rng_), i});
    }

    while (!queue_.empty()) {
        SimEvent e = queue_.top();
        queue_.pop();
        if (e.time > duration) break;

        // Advance the world to this moment before the agent sees it.
        double dt = e.time - now_;
        now_ = e.time;
        step_fundamental(dt);

        // Snapshot the mid at every fixed time step we've crossed. Done before
        // the agent acts, so the sample reflects the book as it stood.
        while (price_dt_ > 0.0 && next_price_t_ <= now_) {
            price_series_.push_back(mid_price());
            next_price_t_ += price_dt_;
        }

        agents_[e.agent_index]->act(*this);

        // Reschedule AFTER acting, from the updated clock.
        queue_.push(SimEvent{now_ + agents_[e.agent_index]->next_delay(rng_),
                             e.agent_index});

        // Sample the state periodically rather than every event.
        if (tracked_ != 0) {
            inv_abs_sum_ += std::abs(static_cast<double>(positions_[tracked_].inventory));
            ++inv_samples_;
        }
        if (++event_count_ % 100 == 0) {
            const Position& p = positions_[tracked_];
            log_.push_back({now_, mid_price(), fundamental_,
                            static_cast<double>(p.inventory),
                            p.cash + p.inventory * mid_price()});
        }
    }
}

uint64_t Market::submit(Order o, uint64_t agent_id) {
    o.id = next_order_id_++;
    order_owner_[o.id] = OrderInfo{agent_id, o.side};   // so on_trade knows who and which side
    book_.add_order(o);
    return o.id;
}

bool Market::cancel(uint64_t order_id) {
    return book_.cancel_order(order_id);
}

double Market::mid_price() const {
    auto b = book_.best_bid();
    auto a = book_.best_ask();
    if (b && a) return (static_cast<double>(*b) + static_cast<double>(*a)) / 2.0;
    if (b) return static_cast<double>(*b);
    if (a) return static_cast<double>(*a);
    return fundamental_;   // empty book: nothing better to say
}

const Position& Market::position(uint64_t agent_id) const {
    static const Position empty{};
    auto it = positions_.find(agent_id);
    return it == positions_.end() ? empty : it->second;
}

void Market::on_trade(uint64_t resting_id, uint64_t incoming_id,
                      int64_t price, uint32_t qty) {
    // The book only knows order ids. We recorded who submitted each order and
    // which side it was, so we can turn a fill into two position changes.
    auto ro = order_owner_.find(resting_id);
    auto io = order_owner_.find(incoming_id);
    if (ro == order_owner_.end() || io == order_owner_.end()) return;

    uint64_t resting_agent  = ro->second.agent_id;
    uint64_t incoming_agent = io->second.agent_id;
    bool incoming_is_buy    = (io->second.side == Side::Buy);

    double notional = static_cast<double>(price) * qty;
    int64_t q = static_cast<int64_t>(qty);

    // A buyer gains inventory and pays cash; the seller is the mirror image.
    if (incoming_is_buy) {
        positions_[incoming_agent].inventory += q;
        positions_[incoming_agent].cash      -= notional;
        positions_[resting_agent].inventory  -= q;
        positions_[resting_agent].cash       += notional;
    } else {
        positions_[incoming_agent].inventory -= q;
        positions_[incoming_agent].cash      += notional;
        positions_[resting_agent].inventory  += q;
        positions_[resting_agent].cash       -= notional;
    }

    // --- Adverse selection attribution ---------------------------------
    // Total P&L mixes two different things: what the maker earns from
    // uninformed flow, and what it loses to informed flow. Here we split
    // them by looking at who was on the other side of each fill.
    //
    // A fill is valued against the fundamental, not the trade price: buying
    // at 10099 when fair value is 10110 is a gain of 11 per unit, whether or
    // not the mid has caught up yet. That is exactly what adverse selection
    // costs, and marking to the mid would hide it.
    if (maker_id_ != 0 && (incoming_agent == maker_id_ || resting_agent == maker_id_)) {
        uint64_t other = (incoming_agent == maker_id_) ? resting_agent : incoming_agent;
        bool maker_bought = (incoming_agent == maker_id_) ? incoming_is_buy : !incoming_is_buy;

        double edge_per_unit = maker_bought ? (fundamental_ - price)
                                            : (price - fundamental_);
        double edge = edge_per_unit * q;

        if (informed_ids_.count(other)) { maker_vs_informed_ += edge; qty_vs_informed_ += q; }
        else                            { maker_vs_noise_    += edge; qty_vs_noise_    += q; }
    }
}

void Market::step_fundamental(double dt) {
    if (dt <= 0.0) return;
    std::normal_distribution<double> z(0.0, 1.0);
    // Random walk: variance grows with time, so the step scales with sqrt(dt).
    fundamental_ += fundamental_vol_ * std::sqrt(dt) * z(rng_);
}

void Market::dump_csv(const std::string& path) const {
    std::ofstream out(path);
    out << "time,mid,fundamental,inventory,mark_to_market\n";
    for (const auto& row : log_) {
        out << row[0] << ',' << row[1] << ',' << row[2] << ','
            << row[3] << ',' << row[4] << '\n';
    }
}

void Market::dump_prices(const std::string& path) const {
    std::ofstream out(path);
    out << "t,mid\n";
    for (size_t i = 0; i < price_series_.size(); ++i)
        out << (i + 1) * price_dt_ << ',' << price_series_[i] << '\n';
}
