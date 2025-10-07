#pragma once
#include <cstdint>
#include <vector>

enum class OrderType { GoodTillCancel, FillAndKill };
enum class Side { Buy, Sell };

using Price    = int32_t;
using Quantity = uint32_t;
using OrderId  = uint64_t;

// Simple trade info struct
struct TradeInfo {
    OrderId orderId;
    Price   price;
    Quantity qty;
};

// Pair of bid/ask trades for a match
struct Trade {
    TradeInfo bid;
    TradeInfo ask;
};

using Trades = std::vector<Trade>;

// -------------------- Orderbook Interface --------------------
class Orderbook {
public:
    Orderbook();
    ~Orderbook();

    // Create a new order object (allocated inside)
    struct Order* MakeOrder(OrderType type, OrderId id, Side side, Price px, Quantity qty);

    // Add order to the book (and match if possible)
    Trades AddOrder(Order* order);

    // Cancel order by id
    void CancelOrder(OrderId id);

    // Number of active orders
    size_t size() const;

private:
    struct Impl;
    Impl* pImpl; // opaque pointer to implementation
};
