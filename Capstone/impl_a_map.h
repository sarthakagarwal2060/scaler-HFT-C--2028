#ifndef IMPL_A_MAP_H
#define IMPL_A_MAP_H

#include "orderbook.h"
#include <map>
#include <unordered_map>
#include <functional>

class OrderBookA : public IOrderBook {
private:
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::unordered_map<uint64_t, OrderRec> orders;

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        int tick = price_to_tick(price);
        orders[id] = OrderRec{ side, tick, qty };
        if (side == Side::Buy)
            bids[tick] += qty;
        else
            asks[tick] += qty;
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders.find(id);
        if (it == orders.end())
            return false;

        OrderRec rec = it->second;
        if (rec.side == Side::Buy) {
            auto lv = bids.find(rec.tick);
            lv->second -= rec.qty;
            if (lv->second == 0)
                bids.erase(lv);
        } else {
            auto lv = asks.find(rec.tick);
            lv->second -= rec.qty;
            if (lv->second == 0)
                asks.erase(lv);
        }
        orders.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO out{};
        if (!bids.empty()) {
            auto it = bids.begin();
            out.has_bid = true;
            out.bid_price = tick_to_price(it->first);
            out.bid_qty = it->second;
        }
        if (!asks.empty()) {
            auto it = asks.begin();
            out.has_ask = true;
            out.ask_price = tick_to_price(it->first);
            out.ask_qty = it->second;
        }
        return out;
    }

    std::vector<Level> get_bids(int depth) const override {
        std::vector<Level> out;
        int c = 0;
        for (auto it = bids.begin(); it != bids.end() && c < depth; ++it, ++c)
            out.push_back(Level{ tick_to_price(it->first), it->second });
        return out;
    }

    std::vector<Level> get_asks(int depth) const override {
        std::vector<Level> out;
        int c = 0;
        for (auto it = asks.begin(); it != asks.end() && c < depth; ++it, ++c)
            out.push_back(Level{ tick_to_price(it->first), it->second });
        return out;
    }
};

#endif
