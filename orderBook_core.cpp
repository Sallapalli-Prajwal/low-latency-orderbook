#include "orderBook_core.hpp"
#include <map>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

struct Order {
    OrderType type;
    OrderId id;
    Side side;
    Price px;
    Quantity initial;
    Quantity remaining;

    bool filled() const { return remaining == 0; }
    void fill(Quantity q) {
        if (q > remaining) throw std::logic_error("overfill");
        remaining -= q;
    }
};

// -------------------- Implementation --------------------
struct Orderbook::Impl {
    using OrderPtr = Order*;
    using Q = std::list<OrderPtr>;

    std::map<Price, Q, std::greater<Price>> bids;
    std::map<Price, Q, std::less<Price>>    asks;
    std::unordered_map<OrderId, std::pair<OrderPtr, Q::iterator>> lookup;

    Trades match() {
        Trades trades;
        while (!bids.empty() && !asks.empty()) {
            auto& [bidPx, bidQ] = *bids.begin();
            auto& [askPx, askQ] = *asks.begin();
            if (bidPx < askPx) break;

            auto* bid = bidQ.front();
            auto* ask = askQ.front();
            Quantity q = std::min(bid->remaining, ask->remaining);
            bid->fill(q);
            ask->fill(q);

            trades.push_back({ {bid->id,bid->px,q}, {ask->id,ask->px,q} });

            if (bid->filled()) { lookup.erase(bid->id); bidQ.pop_front(); }
            if (ask->filled()) { lookup.erase(ask->id); askQ.pop_front(); }

            if (bidQ.empty()) bids.erase(bidPx);
            if (askQ.empty()) asks.erase(askPx);
        }
        return trades;
    }
};

// -------------------- Interface methods --------------------
Orderbook::Orderbook() : pImpl(new Impl) {}
Orderbook::~Orderbook() { delete pImpl; }

Order* Orderbook::MakeOrder(OrderType t, OrderId id, Side s, Price px, Quantity qty) {
    return new Order{t,id,s,px,qty,qty};
}

Trades Orderbook::AddOrder(Order* o) {
    if (pImpl->lookup.count(o->id)) return {};

    Trades trades;

    // select side-specific book
    if (o->side == Side::Buy) {
        auto& q = pImpl->bids[o->px];
        q.push_back(o);
        auto it = std::prev(q.end());
        pImpl->lookup.emplace(o->id, std::make_pair(o, it));
    } else {
        auto& q = pImpl->asks[o->px];
        q.push_back(o);
        auto it = std::prev(q.end());
        pImpl->lookup.emplace(o->id, std::make_pair(o, it));
    }

    // run matcher
    auto newTrades = pImpl->match();
    trades.insert(trades.end(), newTrades.begin(), newTrades.end());

    // handle FAK (FillAndKill)
    if (o->type == OrderType::FillAndKill && o->remaining > 0) {
        CancelOrder(o->id);
    }
    return trades;
}

void Orderbook::CancelOrder(OrderId id) {
    auto it = pImpl->lookup.find(id);
    if (it == pImpl->lookup.end()) return;

    auto* o = it->second.first;
    if (o->side == Side::Buy) {
        auto mapIt = pImpl->bids.find(o->px);
        if (mapIt != pImpl->bids.end()) {
            auto& q = mapIt->second;
            q.erase(it->second.second);
            if (q.empty()) pImpl->bids.erase(mapIt);
        }
    } else {
        auto mapIt = pImpl->asks.find(o->px);
        if (mapIt != pImpl->asks.end()) {
            auto& q = mapIt->second;
            q.erase(it->second.second);
            if (q.empty()) pImpl->asks.erase(mapIt);
        }
    }

    pImpl->lookup.erase(it);
    delete o;
}

size_t Orderbook::size() const { return pImpl->lookup.size(); }
