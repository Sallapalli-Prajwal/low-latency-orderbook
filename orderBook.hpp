// orderBook.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <iostream>
using namespace std;

enum class OrderType { GoodTillCancel, FillAndKill };
enum class Side { Buy, Sell };
using Price = int32_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

class Order;
using OrderPointer = Order*;

class Orderbook {
public:
    Order* MakeOrder(OrderType type, OrderId id, Side s, Price p, Quantity q);
    std::vector<int> AddOrder(Order* o);  // dummy placeholder, adjust to your implementation
    void CancelOrder(OrderId id);
    size_t size() const;
};
