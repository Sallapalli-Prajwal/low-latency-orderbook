// orderBook_multi.cpp
// Multi-symbol interactive exchange simulator (Windows / PowerShell friendly)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <thread>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <conio.h>   // _kbhit(), _getch() on Windows

using namespace std;

// ---------- ANSI COLORS ----------
namespace Color {
    const string RESET  = "\033[0m";
    const string GREEN  = "\033[1;32m";
    const string RED    = "\033[1;31m";
    const string CYAN   = "\033[1;36m";
    const string YELLOW = "\033[1;33m";
    const string GRAY   = "\033[90m";
    const string MAGENTA= "\033[1;35m";
}

// ---------- Time Helper ----------
static inline uint64_t now_ns() {
    return chrono::duration_cast<chrono::nanoseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------- Core Types ----------
enum class Side : uint8_t { Buy, Sell };
enum class OrderType : uint8_t { GTC, IOC };
using Price = int32_t;
using Qty   = uint32_t;
using Oid   = uint64_t;

struct TradeEvent {
    Oid bidId{}, askId{};
    Price px{};
    Qty qty{};
    uint64_t ts_ns{};
    Side aggressor;
};

// ---------- Orderbook (single symbol) ----------
class Orderbook {
    struct Order { Oid id; Side side; OrderType type; Price px; Qty rem; };
    using Q = list<Order>;
    map<Price, Q, greater<Price>> bids_;
    map<Price, Q, less<Price>>    asks_;
public:
    vector<TradeEvent> add(Order o) {
        vector<TradeEvent> out;

        auto matchable = [&]() {
            if (o.side == Side::Buy) return !asks_.empty() && o.px >= asks_.begin()->first;
            return !bids_.empty() && o.px <= bids_.begin()->first;
        };
        auto enqueue = [&]() {
            if (o.side == Side::Buy) bids_[o.px].push_back(o);
            else                     asks_[o.px].push_back(o);
        };

        if (o.type == OrderType::IOC && !matchable()) return out;

        if (o.side == Side::Buy) {
            while (o.rem && !asks_.empty() && o.px >= asks_.begin()->first) {
                auto apx = asks_.begin()->first;
                auto& aq = asks_.begin()->second;
                auto& top = aq.front();
                Qty q = min(o.rem, top.rem);
                o.rem -= q; top.rem -= q;
                out.push_back({o.id, top.id, apx, q, now_ns(), Side::Buy});
                if (top.rem == 0) { aq.pop_front(); if (aq.empty()) asks_.erase(apx); }
                if (!o.rem) break;
            }
            if (o.rem && o.type == OrderType::GTC) enqueue();
        } else {
            while (o.rem && !bids_.empty() && o.px <= bids_.begin()->first) {
                auto bpx = bids_.begin()->first;
                auto& bq = bids_.begin()->second;
                auto& top = bq.front();
                Qty q = min(o.rem, top.rem);
                o.rem -= q; top.rem -= q;
                out.push_back({top.id, o.id, bpx, q, now_ns(), Side::Sell});
                if (top.rem == 0) { bq.pop_front(); if (bq.empty()) bids_.erase(bpx); }
                if (!o.rem) break;
            }
            if (o.rem && o.type == OrderType::GTC) enqueue();
        }
        return out;
    }

    void seedAsks(int levels = 10, int qty = 10) {
        for (int i = 0; i < levels; ++i) {
            Price px = (Price)(100 + i);
            asks_[px].push_back({(Oid)(100000 + i), Side::Sell, OrderType::GTC, px, (Qty)qty});
        }
    }

    Price bestBid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
    Price bestAsk() const { return asks_.empty() ? 0 : asks_.begin()->first; }

    size_t restingOrders() const {
        size_t total = 0;
        for (auto& p : bids_) total += p.second.size();
        for (auto& p : asks_) total += p.second.size();
        return total;
    }

    vector<pair<Price, Qty>> topBids(size_t N=5) const {
        vector<pair<Price, Qty>> res;
        for (auto it = bids_.begin(); it != bids_.end() && res.size() < N; ++it) {
            Qty sum = 0; for (auto& o : it->second) sum += o.rem;
            res.emplace_back(it->first, sum);
        }
        return res;
    }
    vector<pair<Price, Qty>> topAsks(size_t N=5) const {
        vector<pair<Price, Qty>> res;
        for (auto it = asks_.begin(); it != asks_.end() && res.size() < N; ++it) {
            Qty sum = 0; for (auto& o : it->second) sum += o.rem;
            res.emplace_back(it->first, sum);
        }
        return res;
    }
};

// ---------- Per-symbol Market (book + tape + lock) ----------
struct Market {
    Orderbook book;
    deque<TradeEvent> tape;    // scrolling trade tape
    uint64_t tradeCount{0};
    mutex m;
};
static const size_t MAX_TAPE = 12;

// ---------- Symbol Manager ----------
struct SymbolManager {
    vector<string> symbols {"AAPL","MSFT","BTCUSD"};
    unordered_map<string, unique_ptr<Market>> markets;
    atomic<int> activeIdx{0}; // 0=AAPL, 1=MSFT, 2=BTCUSD

    SymbolManager() {
        for (auto& s : symbols) {
            auto m = make_unique<Market>();
            m->book.seedAsks(15, 20);
            markets.emplace(s, std::move(m));
        }
    }

    string activeSymbol() const {
        int idx = activeIdx.load(memory_order_relaxed);
        idx = max(0, min(idx, (int)symbols.size()-1));
        return symbols[(size_t)idx];
    }
    Market& activeMarket() {
        return *markets[activeSymbol()];
    }
};

// ---------- UI Printer (snapshot) ----------
static void printMarketSnapshot(const string& sym, const Market& mk) {
    // Build snapshot while holding lock at call site
    auto bids = mk.book.topBids();
    auto asks = mk.book.topAsks();

    cout << "\033[2J\033[H"; // clear screen
    cout << Color::CYAN << "=========== " << sym << " ORDER BOOK (Top 5) ===========" << Color::RESET << "\n";
    cout << Color::YELLOW << left << setw(15) << "BID_QTY" << setw(10) << "BID_PX"
         << " | " << setw(10) << "ASK_PX" << setw(15) << "ASK_QTY" << Color::RESET << "\n";
    cout << Color::GRAY << "---------------------------------------------------------" << Color::RESET << "\n";

    size_t maxRows = max(bids.size(), asks.size());
    for (size_t i = 0; i < maxRows; ++i) {
        string bidQty = (i < bids.size() ? to_string(bids[i].second) : "");
        string bidPx  = (i < bids.size() ? to_string(bids[i].first)  : "");
        string askPx  = (i < asks.size() ? to_string(asks[i].first)  : "");
        string askQty = (i < asks.size() ? to_string(asks[i].second) : "");
        cout << Color::GREEN << left << setw(15) << bidQty << setw(10) << bidPx << Color::RESET
             << " | "
             << Color::RED   << setw(10) << askPx  << setw(15) << askQty << Color::RESET << "\n";
    }

    cout << Color::GRAY << "---------------------------------------------------------" << Color::RESET << "\n";
    cout << Color::CYAN
         << "Trades=" << mk.tradeCount
         << "  Resting=" << mk.book.restingOrders()
         << "  Top=(" << mk.book.bestBid() << "," << mk.book.bestAsk() << ")"
         << "  Spread=" << (mk.book.bestAsk() - mk.book.bestBid())
         << Color::RESET << "\n";
    cout << Color::YELLOW
         << "Commands: [1]AAPL  [2]MSFT  [3]BTCUSD   [B]uy  [S]ell  [C]ancel  [Q]uit\n"
         << Color::RESET;

    // Tape
    cout << Color::MAGENTA << "\nRecent Trades (" << sym << "):\n" << Color::RESET;
    cout << Color::GRAY << "---------------------------------------------------------" << Color::RESET << "\n";
    for (auto& t : mk.tape) {
        const string& col = (t.aggressor == Side::Buy ? Color::GREEN : Color::RED);
        cout << col << setw(6) << (t.aggressor == Side::Buy ? "BUY" : "SELL") << Color::RESET
             << " @ " << setw(5) << t.px
             << " x " << setw(5) << t.qty
             << Color::GRAY << "  id(" << t.bidId << "," << t.askId << ")" << Color::RESET << "\n";
    }
    cout.flush();
}

// ---------- Tape helper ----------
static inline void addTradesToTapeLocked(Market& mk, const vector<TradeEvent>& trades) {
    for (auto& t : trades) {
        mk.tape.push_front(t);
        if (mk.tape.size() > MAX_TAPE) mk.tape.pop_back();
    }
}

// ---------- Display Thread (refresh current symbol) ----------
static void displayLoop(SymbolManager& sm, atomic<bool>& runFlag) {
    while (runFlag) {
        string sym = sm.activeSymbol();
        {
            // Snapshot under lock
            auto& mk = *sm.markets[sym];
            lock_guard<mutex> g(mk.m);
            printMarketSnapshot(sym, mk);
        }
        this_thread::sleep_for(chrono::milliseconds(500));
    }
}

// ---------- Input Thread (switch symbols + manual orders) ----------
static void inputLoop(SymbolManager& sm, atomic<bool>& runFlag) {
    static uint64_t userId = 900000;
    while (runFlag) {
        if (_kbhit()) {
            char ch = toupper(_getch());
            if (ch == 'Q') { runFlag = false; break; }
            else if (ch == '1') sm.activeIdx.store(0);
            else if (ch == '2') sm.activeIdx.store(1);
            else if (ch == '3') sm.activeIdx.store(2);
            else if (ch == 'B' || ch == 'S' || ch == 'C') {
                string sym = sm.activeSymbol();
                auto& mk = *sm.markets[sym];
                lock_guard<mutex> g(mk.m);

                vector<TradeEvent> trades;
                if (ch == 'B') {
                    Price px = mk.book.bestAsk() ? (Price)(mk.book.bestAsk() - 2) : (Price)99;
                    trades = mk.book.add({++userId, Side::Buy, OrderType::GTC, px, (Qty)10});
                    for (auto& t : trades) t.aggressor = Side::Buy; // show user side on tape
                }
                else if (ch == 'S') {
                    Price px = mk.book.bestBid() ? (Price)(mk.book.bestBid() + 5) : (Price)110; // make it rest
                    trades = mk.book.add({++userId, Side::Sell, OrderType::GTC, px, (Qty)10});
                    for (auto& t : trades) t.aggressor = Side::Sell;
                }
                else if (ch == 'C') {
                    Price a = mk.book.bestAsk();
                    if (a) trades = mk.book.add({++userId, Side::Buy, OrderType::IOC, a, (Qty)1});
                    // (simple IOC poke to simulate a cancel-take)
                }

                mk.tradeCount += trades.size();
                addTradesToTapeLocked(mk, trades);
            }
        }
        this_thread::sleep_for(chrono::milliseconds(40));
    }
}

// ---------- Per-symbol market simulation threads ----------
static void marketSimLoop(SymbolManager& sm, const string sym, atomic<bool>& runFlag, int seedSkew=0) {
    uint64_t id = 1;
    bool flip = (seedSkew % 2) != 0;
    auto& mk = *sm.markets[sym];

    while (runFlag) {
        Side s = (flip ? Side::Sell : Side::Buy);
        flip = !flip;

        vector<TradeEvent> trades;
        {
            lock_guard<mutex> g(mk.m);
            Price px = (Price)(100 + (id % 30) + seedSkew); // different centers per symbol
            trades = mk.book.add({id++, s, OrderType::IOC, px, (Qty)10});
            mk.tradeCount += trades.size();
            addTradesToTapeLocked(mk, trades);
        }
        this_thread::sleep_for(chrono::milliseconds(25 + (seedSkew % 10))); // slight variation per symbol
    }
}

// ---------- Main ----------
int main() {
    // Use Windows Terminal / PowerShell for ANSI colors
    SymbolManager sm;
    atomic<bool> runFlag{true};

    // Threads: display + input + one marketSim per symbol
    thread tDisp(displayLoop, ref(sm), ref(runFlag));
    thread tIn(inputLoop, ref(sm), ref(runFlag));

    vector<thread> sims;
    // different "centers" for symbols so they don't look identical
    sims.emplace_back(marketSimLoop, ref(sm), string("AAPL"),  ref(runFlag), 0);
    sims.emplace_back(marketSimLoop, ref(sm), string("MSFT"),  ref(runFlag), 3);
    sims.emplace_back(marketSimLoop, ref(sm), string("BTCUSD"),ref(runFlag), 8);

    // Wait for quit
    tIn.join();
    runFlag = false;

    // Join all threads
    for (auto& th : sims) th.join();
    tDisp.join();

    // Final stats
    cout << "\n=== FINAL SUMMARY ===\n";
    for (size_t i = 0; i < sm.symbols.size(); ++i) {
        const string& sym = sm.symbols[i];
        auto& mk = *sm.markets[sym];
        lock_guard<mutex> g(mk.m);
        cout << sym << ": trades=" << mk.tradeCount
             << " resting=" << mk.book.restingOrders()
             << " top=(" << mk.book.bestBid() << "," << mk.book.bestAsk() << ")"
             << " spread=" << (mk.book.bestAsk() - mk.book.bestBid()) << "\n";
    }
    cout << "======================\n";
    return 0;
}
