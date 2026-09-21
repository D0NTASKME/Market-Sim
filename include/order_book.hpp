#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <iostream>

enum class Side { Buy, Sell };
struct Order {
    uint64_t id;
    Side side;
    int64_t price_ticks;
    uint32_t quantity;
};
struct OrderLocation {
    Side side;
    int64_t price_ticks;
    std::list<Order>::iterator it;
};
class OrderBook {
public:
    using TradeCallback = std::function<void(uint64_t resting_id, uint64_t incoming_id,int64_t price, uint32_t qty)>;
    OrderBook() { index_.reserve(2'000'000); }
    void set_trade_callback(TradeCallback cb) { on_trade_ = std::move(cb); }
    void match_buy(Order& incoming) {
        while(incoming.quantity > 0 && !asks_.empty()){
            auto it = asks_.begin();
            if (it->first > incoming.price_ticks) break;
            auto& orders = it->second;
            auto& resting = orders.front();

            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if (on_trade_) on_trade_(resting.id, incoming.id, it->first, traded);
            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) {
                index_.erase(resting.id);
                orders.pop_front();}
            if (orders.empty()) {
                asks_.erase(it);              // it is discarded right afte
            }

        }
    }
    void match_sell(Order& incoming){
        while(incoming.quantity > 0 && !bids_.empty()){
            auto it = bids_.begin();
            if (it->first < incoming.price_ticks) break;
            auto& orders = it->second;
            auto& resting = orders.front();

            uint32_t traded = std::min(resting.quantity, incoming.quantity);
            if (on_trade_) on_trade_(resting.id, incoming.id, it->first, traded);

            resting.quantity -= traded;
            incoming.quantity -= traded;

            if (resting.quantity == 0) {
                index_.erase(resting.id);
                orders.pop_front();

            }
            if (orders.empty()) {
                bids_.erase(it);              // it is discarded right afte
            }
        }
    }
    void add_order(Order o) {          // by value — we own it, we mutate it
    if (o.side == Side::Buy) {
        match_buy(o);
        if (o.quantity > 0){
            auto& level = bids_[o.price_ticks];
            level.push_back(o);
            index_[o.id] = OrderLocation{Side::Buy, o.price_ticks, std::prev(level.end())};
        }
    } else {
        match_sell(o);

        if (o.quantity > 0){
            auto& level = asks_[o.price_ticks];
            level.push_back(o);
            index_[o.id] = OrderLocation{Side::Sell, o.price_ticks, std::prev(level.end())};
        }
    }
    }
    bool cancel_order(uint64_t id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;

        const auto& loc = found->second;
        if (loc.side == Side::Buy) {
            auto level = bids_.find(loc.price_ticks);
            level->second.erase(loc.it);
            if (level->second.empty()) bids_.erase(level);
        } else {
            auto level = asks_.find(loc.price_ticks);
            level->second.erase(loc.it);
            if (level->second.empty()) asks_.erase(level);
        }
        index_.erase(found);
        return true;
    }
    bool naive_cancel_order(uint64_t id) {
        for (auto level = asks_.begin(); level != asks_.end(); ++level) {
            auto& orders = level->second;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->id == id) {
                    index_.erase(id);
                    orders.erase(it);
                    if (orders.empty()) asks_.erase(level);
                    return true;
                }
            }
        }
        for (auto level = bids_.begin(); level != bids_.end(); ++level) {
            auto& orders = level->second;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->id == id) {
                    index_.erase(id);
                    orders.erase(it);
                    if (orders.empty()) bids_.erase(level);
                    return true;
                }
            }
        }
        return false;

    }

    std::optional<int64_t> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
        }
    std::optional<int64_t> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin() -> first;
    }
    static void print_level(int64_t price, const std::list<Order>& orders) {
        uint32_t total{0};
        int count{0};
        for (const Order& o : orders) { total += o.quantity; count++; }
        std::cout << price << "  " << total << "  (" << count << ")\n";
    }
    size_t index_size(){
        return index_.size();
    }
    void print_book() const{
        for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
            const auto& price  = it->first;
            const auto& orders = it->second;
            print_level(price, orders);

        }
        std::cout << " ------------------------\n";
        for(const auto& [price,orders]  : bids_){
            print_level(price, orders);
        }
    }
    uint32_t total_quantity_at(Side side, int64_t price) const {
        uint32_t total = 0;
        if (side == Side::Sell) {
            auto it = asks_.find(price);
            if (it == asks_.end()) return 0;
            for (const Order& o : it->second) total += o.quantity;
        } else {
            auto it = bids_.find(price);
            if (it == bids_.end()) return 0;
            for (const Order& o : it->second) total += o.quantity;
        }
        return total;
    }
    size_t order_count_at(Side side, int64_t price) const {
        if (side == Side::Sell) { auto it = asks_.find(price); return it == asks_.end() ? 0 : it->second.size(); }
        auto it = bids_.find(price); return it == bids_.end() ? 0 : it->second.size();
    }
    std::optional<uint64_t> front_order_id_at(Side side, int64_t price) const {
        if (side == Side::Sell) { auto it = asks_.find(price); if (it == asks_.end() || it->second.empty()) return std::nullopt; return it->second.front().id; }
        auto it = bids_.find(price); if (it == bids_.end() || it->second.empty()) return std::nullopt; return it->second.front().id;
    }
    size_t level_count(Side side) const { return side == Side::Sell ? asks_.size() : bids_.size(); }

private:
    std::map<int64_t, std::list<Order>> asks_;
    std::map<int64_t, std::list<Order>, std::greater<>> bids_;
    std::unordered_map<uint64_t, OrderLocation> index_;
    TradeCallback on_trade_;
};
