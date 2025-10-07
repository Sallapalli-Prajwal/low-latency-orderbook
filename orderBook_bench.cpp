#include "orderBook_core.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iomanip>
using namespace std;

// ---------- Functional Testcases ----------
void runBasicTests(Orderbook& ob) {
    cout << "\n=== FUNCTIONAL TESTS ===\n";

    // 1. Add a buy order
    auto* o1 = ob.MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    auto t1 = ob.AddOrder(o1);
    cout << "Added BUY 100x10, trades executed = " << t1.size() << "\n";

    // 2. Add a sell order that matches
    auto* o2 = ob.MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 99, 10);
    auto t2 = ob.AddOrder(o2);
    cout << "Added SELL 99x10, trades executed = " << t2.size() << "\n";

    // 3. Add and cancel
    auto* o3 = ob.MakeOrder(OrderType::GoodTillCancel, 3, Side::Buy, 101, 5);
    ob.AddOrder(o3);
    ob.CancelOrder(3);
    cout << "Cancelled order #3, book size now = " << ob.size() << "\n";

    cout << "========================\n";
}

// ---------- Latency Benchmark ----------
void benchmarkLatency(Orderbook& ob, size_t nOrders = 100000) {
    vector<double> lat;
    lat.reserve(nOrders);

    auto startAll = chrono::high_resolution_clock::now();

    for (size_t i = 0; i < nOrders; ++i) {
        auto t1 = chrono::high_resolution_clock::now();

        Side s = (i % 2 == 0 ? Side::Buy : Side::Sell);
        Price px = (s == Side::Buy ? 100 + (i % 5) : 101 + (i % 5));
        Quantity qty = 10;
        auto* o = ob.MakeOrder(OrderType::GoodTillCancel, i + 10'000, s, px, qty);
        ob.AddOrder(o);

        auto t2 = chrono::high_resolution_clock::now();
        lat.push_back(chrono::duration<double, nano>(t2 - t1).count());
    }

    auto endAll = chrono::high_resolution_clock::now();
    double totalSec = chrono::duration<double>(endAll - startAll).count();

    sort(lat.begin(), lat.end());
    double p50 = lat[nOrders / 2];
    double p99 = lat[(size_t)(nOrders * 0.99)];
    double avg = accumulate(lat.begin(), lat.end(), 0.0) / lat.size();

    cout << fixed << setprecision(2);
    cout << "\n=== LATENCY BENCHMARK ===\n";
    cout << "Orders tested  : " << nOrders << "\n";
    cout << "Throughput     : " << (nOrders / totalSec) << " orders/sec\n";
    cout << "Avg latency    : " << avg << " ns\n";
    cout << "p50 latency    : " << p50 << " ns\n";
    cout << "p99 latency    : " << p99 << " ns\n";
    cout << "==========================\n";
}

// ---------- Main ----------
int main() {
    cout << "=== ORDERBOOK TEST & BENCH ===\n";
    Orderbook ob;
    runBasicTests(ob);
    benchmarkLatency(ob, 500000);
}
