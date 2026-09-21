#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

enum class Side { Buy, Sell };
struct Order { uint64_t id; Side side; int64_t price_ticks; uint32_t quantity; };
struct OrderLocation { Side side; int64_t price_ticks; std::list<Order>::iterator it; };

class OrderBook {
public:
    using TradeCallback = std::function<void(uint64_t, uint64_t, int64_t, uint32_t)>;
    OrderBook() { index_.reserve(1'000'000); }
    void set_trade_callback(TradeCallback cb) { on_trade_ = std::move(cb); }
    void add_order(Order o) {
        if (o.side == Side::Buy) { match_buy(o); if (o.quantity > 0) { auto& l = bids_[o.price_ticks]; l.push_back(o); index_[o.id] = {Side::Buy, o.price_ticks, std::prev(l.end())}; } }
        else { match_sell(o); if (o.quantity > 0) { auto& l = asks_[o.price_ticks]; l.push_back(o); index_[o.id] = {Side::Sell, o.price_ticks, std::prev(l.end())}; } }
    }
    bool cancel_order(uint64_t id) {
        auto f = index_.find(id); if (f == index_.end()) return false;
        const auto& loc = f->second;
        if (loc.side == Side::Buy) { auto l = bids_.find(loc.price_ticks); l->second.erase(loc.it); if (l->second.empty()) bids_.erase(l); }
        else { auto l = asks_.find(loc.price_ticks); l->second.erase(loc.it); if (l->second.empty()) asks_.erase(l); }
        index_.erase(f); return true;
    }
    std::optional<int64_t> best_bid() const { if (bids_.empty()) return std::nullopt; return bids_.begin()->first; }
    // Visit up to n non-empty levels from the best price outward.
    template <typename F>
    void for_each_level(Side side, size_t n, F&& f) const {
        size_t k = 0;
        if (side == Side::Buy) {
            for (auto it = bids_.begin(); it != bids_.end() && k < n; ++it, ++k) f(it->first, it->second);
        } else {
            for (auto it = asks_.begin(); it != asks_.end() && k < n; ++it, ++k) f(it->first, it->second);
        }
    }
    std::optional<int64_t> best_ask() const { if (asks_.empty()) return std::nullopt; return asks_.begin()->first; }
private:
    void match_buy(Order& in) {
        while (in.quantity > 0 && !asks_.empty()) {
            auto it = asks_.begin(); if (it->first > in.price_ticks) break;
            auto& orders = it->second; auto& r = orders.front();
            uint32_t t = std::min(r.quantity, in.quantity);
            if (on_trade_) on_trade_(r.id, in.id, it->first, t);
            r.quantity -= t; in.quantity -= t;
            if (r.quantity == 0) { index_.erase(r.id); orders.pop_front(); }
            if (orders.empty()) asks_.erase(it);
        }
    }
    void match_sell(Order& in) {
        while (in.quantity > 0 && !bids_.empty()) {
            auto it = bids_.begin(); if (it->first < in.price_ticks) break;
            auto& orders = it->second; auto& r = orders.front();
            uint32_t t = std::min(r.quantity, in.quantity);
            if (on_trade_) on_trade_(r.id, in.id, it->first, t);
            r.quantity -= t; in.quantity -= t;
            if (r.quantity == 0) { index_.erase(r.id); orders.pop_front(); }
            if (orders.empty()) bids_.erase(it);
        }
    }
    std::map<int64_t, std::list<Order>> asks_;
    std::map<int64_t, std::list<Order>, std::greater<>> bids_;
    std::unordered_map<uint64_t, OrderLocation> index_;
    TradeCallback on_trade_;
};
