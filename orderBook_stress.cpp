#include "orderBook_core.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <atomic>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <windows.h>
#include <psapi.h>

using namespace std;

// ---------- Simple RNG ----------
static inline int randBetween(int min, int max) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

// ---------- Latency stats ----------
struct LatencyStats {
    vector<double> samples; // ns per op
    uint64_t tradeCount = 0;
    void add(double ns) { samples.push_back(ns); }
    void addTrades(size_t n) { tradeCount += n; }

    void summarize(int threadId) {
        if (samples.empty()) return;
        sort(samples.begin(), samples.end());
        double p50 = samples[samples.size() / 2];
        double p99 = samples[(size_t)(samples.size() * 0.99)];
        double avg = accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        cout << fixed << setprecision(2);
        cout << "Thread " << threadId
             << " | ops=" << samples.size()
             << " trades=" << tradeCount
             << " avg=" << avg << "ns p50=" << p50 << "ns p99=" << p99 << "ns\n";
    }
};

// ---------- Stress Worker ----------
void stressWorker(Orderbook& ob, size_t nOps, atomic<uint64_t>& tradeCount,
                  int threadId, vector<LatencyStats>& allStats, mutex& obLock)
{
    cout << "[Thread " << threadId << "] started\n" << flush;

    LatencyStats stats;
    try {
        for (size_t i = 0; i < nOps; ++i) {
            auto t1 = chrono::high_resolution_clock::now();

            Side s = (randBetween(0, 1) ? Side::Buy : Side::Sell);
            Price px = (s == Side::Buy ? 100 + randBetween(0, 20)
                                       : 101 + randBetween(0, 20));
            Quantity qty = randBetween(1, 50);

            auto* o = ob.MakeOrder(OrderType::GoodTillCancel,
                                   (threadId * 10'000'000ULL) + i, s, px, qty);

            // protect shared Orderbook (non-thread-safe)
            {
                lock_guard<mutex> lock(obLock);
                auto trades = ob.AddOrder(o);
                stats.addTrades(trades.size());
                tradeCount += trades.size();

                // occasional cancels
                if (i % 1000 == 0 && ob.size() > 0) {
                    auto cancelId = (threadId * 10'000'000ULL) + randBetween(0, (int)i);
                    ob.CancelOrder(cancelId);
                }
            }

            auto t2 = chrono::high_resolution_clock::now();
            double ns = chrono::duration<double, nano>(t2 - t1).count();
            stats.add(ns);

            if (threadId == 0 && i % 200000 == 0) cout << "." << flush;
        }
    } catch (const std::exception& e) {
        cerr << "\nThread " << threadId << " exception: " << e.what() << endl;
    }
    allStats[threadId] = move(stats);
}

// ---------- CSV Export ----------
void exportCSV(const vector<LatencyStats>& allStats, const string& filename) {
    ofstream out(filename);
    out << "thread_id,op_index,latency_ns\n";
    for (size_t t = 0; t < allStats.size(); ++t) {
        for (size_t i = 0; i < allStats[t].samples.size(); ++i)
            out << t << "," << i << "," << allStats[t].samples[i] << "\n";
    }
    out.close();
    cout << "\nSaved latency samples to " << filename << endl;
}
// ---------- System Resource Logger ----------
struct ResourceSample {
    double timestamp; // seconds
    double rssMB;     // resident memory (MB)
    double cpuSec;    // total CPU time (s)
};

ResourceSample getResourceUsage(double startTimeSec) {
    FILETIME createTime, exitTime, kernelTime, userTime;
    GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime);

    ULARGE_INTEGER kt, ut;
    kt.LowPart = kernelTime.dwLowDateTime;
    kt.HighPart = kernelTime.dwHighDateTime;
    ut.LowPart = userTime.dwLowDateTime;
    ut.HighPart = userTime.dwHighDateTime;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    double rssMB = pmc.WorkingSetSize / (1024.0 * 1024.0);

    double cpuSec = (kt.QuadPart + ut.QuadPart) / 10'000'000.0; // FILETIME is in 100ns units
    double nowSec = chrono::duration<double>(
                        chrono::steady_clock::now().time_since_epoch())
                        .count();
    return {nowSec - startTimeSec, rssMB, cpuSec};
}

// ---------- Stress Test ----------
void runStressTest(size_t totalOps = 5'000'000, int nThreads = 4, bool dumpCSV = true) {
    cout << "\n=== STRESS TEST START ===" << endl;
    Orderbook ob;
    atomic<uint64_t> tradeCount{0};
    mutex obLock;

    size_t opsPerThread = totalOps / nThreads;
    vector<LatencyStats> allStats(nThreads);

    auto start = chrono::high_resolution_clock::now();
    double startTimeSec = chrono::duration<double>(start.time_since_epoch()).count();

    vector<ResourceSample> sysSamples;
    atomic<bool> monitorRun{true};

    // ---------- Monitor thread ----------
    thread monitor([&]() {
        while (monitorRun) {
            sysSamples.push_back(getResourceUsage(startTimeSec));
            this_thread::sleep_for(chrono::seconds(1));
        }
    });

    // ---------- Launch worker threads ----------
    vector<thread> workers;
    for (int t = 0; t < nThreads; ++t)
        workers.emplace_back(stressWorker, ref(ob), opsPerThread, ref(tradeCount),
                             t, ref(allStats), ref(obLock));

    for (auto& th : workers)
        if (th.joinable()) th.join();

    monitorRun = false;
    if (monitor.joinable()) monitor.join();

    auto end = chrono::high_resolution_clock::now();
    double secs = chrono::duration<double>(end - start).count();

    cout << "\n=== PER-THREAD LATENCY ===" << endl;
    for (int t = 0; t < nThreads; ++t)
        allStats[t].summarize(t);

    uint64_t totalSamples = 0;
    for (auto& s : allStats) totalSamples += s.samples.size();

    cout << "\n=== STRESS SUMMARY ===" << endl;
    cout << fixed << setprecision(2);
    cout << "Threads        : " << nThreads << "\n";
    cout << "Total ops      : " << totalOps << "\n";
    cout << "Total samples  : " << totalSamples << "\n";
    cout << "Total trades   : " << tradeCount.load() << "\n";
    cout << "Final book size: " << ob.size() << "\n";
    cout << "Elapsed time   : " << secs << " s\n";
    cout << "Throughput     : " << (totalOps / secs) << " ops/sec\n";
    cout << "=======================" << endl;

    if (dumpCSV) exportCSV(allStats, "latency_samples.csv");

    // ---------- Export system resource usage ----------
    ofstream sysOut("system_usage.csv");
    sysOut << "time_s,rss_MB,cpu_s\n";
    for (auto& s : sysSamples)
        sysOut << s.timestamp << "," << s.rssMB << "," << s.cpuSec << "\n";
    sysOut.close();
    cout << "Saved system metrics to system_usage.csv\n";
}

// ---------- Main ----------
int main() {
    try {
        std::cout.setf(std::ios::unitbuf);  // auto-flush every << output

        runStressTest(5'000'000, 4, true); // 5M ops, 4 threads, export CSV
    } catch (const std::exception& e) {
        std::cerr << "\n[MAIN THREAD] Exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "\n[MAIN THREAD] Unknown exception" << std::endl;
    }
}

