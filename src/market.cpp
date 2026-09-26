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

void Market::start() {
    if (started_) return;
    started_ = true;
    // Seed: every agent gets a first action time drawn from its own
    // distribution, so they don't all fire at t = 0.
    for (size_t i = 0; i < agents_.size(); ++i) {
        queue_.push(SimEvent{agents_[i]->next_delay(rng_), i});
    }
}

void Market::run(double duration) { run_until(duration); }

void Market::run_until(double until) {
    start();
    // Peek before popping. Popping first and then discovering the event is
    // past `until` would throw that event away, and its agent would never be
    // rescheduled. Harmless for a single run, fatal when called repeatedly.
    while (!queue_.empty() && queue_.top().time <= until) {
        SimEvent e = queue_.top();
        queue_.pop();

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

        // Features are sampled before the agent acts, from state the maker
        // could see. The target (fundamental - mid) is recorded alongside but
        // is never an input.
        recent_mids_.push_back({now_, mid_price()});
        while (!recent_mids_.empty() && recent_mids_.front().first < now_ - 5.0) recent_mids_.pop_front();
        while (!recent_trades_.empty() && recent_trades_.front().first < now_ - 5.0) recent_trades_.pop_front();
        while (feature_dt_ > 0.0 && next_feature_t_ <= now_) {
            features_.push_back(snapshot_features());
            next_feature_t_ += feature_dt_;
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
    book_.add_order(o);   // any executions are logged from on_trade
    // In LOBSTER only the part that rests appears as a new-order message;
    // the marketable part shows up as executions of the orders it hit.
    if (lob_msg_) {
        if (auto r = book_.resting(o.id)) lob_log(1, o.id, r->quantity, r->price_ticks, r->side);
    }
    return o.id;
}

uint64_t Market::submit_ioc(Order o, uint64_t agent_id) {
    o.id = next_order_id_++;
    order_owner_[o.id] = OrderInfo{agent_id, o.side};
    book_.add_order(o);                                    // executions logged from on_trade
    if (book_.resting(o.id)) book_.cancel_order(o.id);     // drop the remainder, unlogged
    return o.id;
}

bool Market::cancel(uint64_t order_id) {
    std::optional<Order> r;
    if (lob_msg_) r = book_.resting(order_id);
    bool ok = book_.cancel_order(order_id);
    if (ok && r) lob_log(3, order_id, r->quantity, r->price_ticks, r->side);
    return ok;
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

    // Count distinct aggressive orders from informed traders that traded.
    if (informed_ids_.count(incoming_agent) && incoming_id != last_informed_order_) {
        ++informed_trades_;
        last_informed_order_ = incoming_id;
    }

    if (lob_msg_) {
        Side resting_side = incoming_is_buy ? Side::Sell : Side::Buy;
        lob_log(4, resting_id, qty, price, resting_side, resting_side, price, qty);
    }

    last_trade_time_ = now_;
    last_trade_sign_ = incoming_is_buy ? 1 : -1;

    // Order flow as the market sees it: which side was aggressive, and how much.
    double signed_q = incoming_is_buy ? q : -q;
    recent_trades_.push_back({now_, signed_q});
    recent_signs_.push_back(incoming_is_buy ? 1 : -1);
    if (recent_signs_.size() > 10) recent_signs_.pop_front();

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

std::vector<DepthLevel> Market::depth(Side side, size_t n) const {
    std::vector<DepthLevel> out;
    book_.for_each_level(side, n, [&](int64_t price, const std::list<Order>& orders) {
        DepthLevel l{price, 0, 0};
        for (const Order& o : orders) {
            l.qty += o.quantity;
            auto it = order_owner_.find(o.id);
            if (it != order_owner_.end() && it->second.agent_id == maker_id_) l.mine += o.quantity;
        }
        out.push_back(l);
    });
    return out;
}

FeatureRow Market::snapshot_features() {
    FeatureRow f{};
    f.t = now_;
    double mid = mid_price();
    for (const auto& [t, q] : recent_trades_) {
        f.ofi_5s += q;
        if (t >= now_ - 1.0) f.ofi_1s += q;
    }
    for (int s : recent_signs_) f.trade_signs_10 += s;

    auto bids = depth(Side::Buy, 1), asks = depth(Side::Sell, 1);
    double bq = bids.empty() ? 0.0 : static_cast<double>(bids[0].qty);
    double aq = asks.empty() ? 0.0 : static_cast<double>(asks[0].qty);
    f.book_imbalance = (bq + aq) > 0 ? (bq - aq) / (bq + aq) : 0.0;
    f.spread = (!bids.empty() && !asks.empty()) ? static_cast<double>(asks[0].price - bids[0].price) : 0.0;

    // recent_mids_ holds (time, mid) for the last 5 seconds, oldest first.
    double mid_5s = recent_mids_.empty() ? mid : recent_mids_.front().second;
    double mid_1s = mid;
    for (const auto& [t, m] : recent_mids_) if (t >= now_ - 1.0) { mid_1s = m; break; }
    f.dmid_1s = mid - mid_1s;
    f.dmid_5s = mid - mid_5s;

    f.maker_inventory = maker_id_ ? static_cast<double>(positions_[maker_id_].inventory) : 0.0;
    f.mid = mid;
    f.target = fundamental_ - mid;
    return f;
}

void Market::enable_lobster_log(const std::string& prefix, int levels) {
    lob_levels_ = levels;
    lob_msg_  = std::make_unique<std::ofstream>(prefix + "_message_" + std::to_string(levels) + ".csv");
    lob_book_ = std::make_unique<std::ofstream>(prefix + "_orderbook_" + std::to_string(levels) + ".csv");
    lob_msg_->precision(10);
}

void Market::lob_log(int type, uint64_t id, uint32_t size, int64_t price, Side side,
                     Side adjust_side, int64_t adjust_price, uint32_t adjust_qty) {
    const double t = 34200.0 + now_;               // seconds after midnight, from 09:30
    const int dir = side == Side::Buy ? 1 : -1;
    *lob_msg_ << t << ',' << type << ',' << id << ',' << size << ',' << price * 100 << ',' << dir << '\n';

    auto asks = depth(Side::Sell, lob_levels_ + 1);
    auto bids = depth(Side::Buy,  lob_levels_ + 1);
    if (adjust_qty > 0) {                          // apply the pending fill
        auto& lv = adjust_side == Side::Sell ? asks : bids;
        for (size_t i = 0; i < lv.size(); ++i) {
            if (lv[i].price != adjust_price) continue;
            lv[i].qty -= adjust_qty;
            if (lv[i].qty <= 0) lv.erase(lv.begin() + static_cast<long>(i));
            break;
        }
    }
    std::ostream& ob = *lob_book_;
    for (int l = 0; l < lob_levels_; ++l) {
        const size_t i = static_cast<size_t>(l);
        if (i < asks.size()) ob << asks[i].price * 100 << ',' << asks[i].qty; else ob << "9999999999,0";
        ob << ',';
        if (i < bids.size()) ob << bids[i].price * 100 << ',' << bids[i].qty; else ob << "-9999999999,0";
        ob << (l + 1 < lob_levels_ ? ',' : '\n');
    }
}
