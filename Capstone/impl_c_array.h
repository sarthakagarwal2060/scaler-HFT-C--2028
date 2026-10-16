#ifndef IMPL_C_ARRAY_H
#define IMPL_C_ARRAY_H

#include "orderbook.h"
#include <vector>

struct OrderRec2 {
    Side side;
    int tick;
    long qty;
    bool live;
};

class OrderBookC : public IOrderBook {
private:
    std::vector<long> bidQty;
    std::vector<long> askQty;
    std::vector<OrderRec2> orders;
    int bestBidIdx;
    int bestAskIdx;

    static const int BID_SLOTS = BID_MAX_TICK - BID_MIN_TICK + 1;
    static const int ASK_SLOTS = ASK_MAX_TICK - ASK_MIN_TICK + 1;

public:
    OrderBookC() {
        bidQty.assign(BID_SLOTS, 0);
        askQty.assign(ASK_SLOTS, 0);
        orders.reserve(1 << 20);
        bestBidIdx = -1;
        bestAskIdx = -1;
    }

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int tick = price_to_tick(price);

        if (id >= orders.size()) {
            size_t ns = orders.size() ? orders.size() : 1024;
            while (ns <= id) ns *= 2;
            orders.resize(ns);
        }
        orders[id] = OrderRec2{ side, tick, qty, true };

        if (side == Side::Buy) {
            int i = tick - BID_MIN_TICK;
            bidQty[i] += qty;
            if (i > bestBidIdx) bestBidIdx = i;
        } else {
            int i = tick - ASK_MIN_TICK;
            askQty[i] += qty;
            if (bestAskIdx == -1 || i < bestAskIdx) bestAskIdx = i;
        }
    }

    bool cancel_order(uint64_t id) override {
        if (id >= orders.size())
            return false;
        OrderRec2 &rec = orders[id];
        if (!rec.live)
            return false;
        rec.live = false;

        if (rec.side == Side::Buy) {
            int i = rec.tick - BID_MIN_TICK;
            bidQty[i] -= rec.qty;
            if (bidQty[i] == 0 && i == bestBidIdx) {
                while (bestBidIdx >= 0 && bidQty[bestBidIdx] == 0)
                    bestBidIdx--;
            }
        } else {
            int i = rec.tick - ASK_MIN_TICK;
            askQty[i] -= rec.qty;
            if (askQty[i] == 0 && i == bestAskIdx) {
                while (bestAskIdx < ASK_SLOTS && askQty[bestAskIdx] == 0)
                    bestAskIdx++;
                if (bestAskIdx == ASK_SLOTS)
                    bestAskIdx = -1;
            }
        }
        return true;
    }

    BBO get_bbo() const override {
        BBO out{};
        if (bestBidIdx >= 0) {
            out.has_bid = true;
            out.bid_price = tick_to_price(bestBidIdx + BID_MIN_TICK);
            out.bid_qty = bidQty[bestBidIdx];
        }
        if (bestAskIdx >= 0) {
            out.has_ask = true;
            out.ask_price = tick_to_price(bestAskIdx + ASK_MIN_TICK);
            out.ask_qty = askQty[bestAskIdx];
        }
        return out;
    }

    std::vector<Level> get_bids(int depth) const override {
        std::vector<Level> out;
        for (int i = bestBidIdx; i >= 0 && (int)out.size() < depth; i--) {
            if (bidQty[i] > 0)
                out.push_back(Level{ tick_to_price(i + BID_MIN_TICK), bidQty[i] });
        }
        return out;
    }

    std::vector<Level> get_asks(int depth) const override {
        std::vector<Level> out;
        if (bestAskIdx < 0) return out;
        for (int i = bestAskIdx; i < ASK_SLOTS && (int)out.size() < depth; i++) {
            if (askQty[i] > 0)
                out.push_back(Level{ tick_to_price(i + ASK_MIN_TICK), askQty[i] });
        }
        return out;
    }
};

#endif
