# OrderBook

A multi-symbol order book and exchange simulator in C++. Run an interactive terminal with live books for **AAPL**, **MSFT**, and **BTCUSD**, place/cancel orders, and watch simulated matching and a scrolling trade tape.

## Features

- **Multi-symbol order books** — Switch between AAPL, MSFT, and BTCUSD
- **Order types** — GTC (Good Till Cancel) and IOC (Immediate Or Cancel)
- **Price-time priority** — Bids/asks stored by price level with FIFO within level
- **Live UI** — Top 5 levels, trade count, spread, and recent trades (ANSI colors)
- **Background simulation** — Per-symbol threads continuously submit IOC orders for demo flow
- **Thread-safe** — Per-symbol mutex for book + tape updates

## Requirements

- C++11 or later
- **Windows** — Interactive app uses `conio.h` (`_kbhit`, `_getch`). Use Windows Terminal or PowerShell for ANSI colors.

## Build

**Interactive simulator** (standalone):

```bash
g++ -std=c++11 -O2 -o orderBook.exe orderBook.cpp
```

**Core library + benchmarks** (optional):

```bash
g++ -std=c++11 -O2 -c orderBook_core.cpp -o orderBook_core.o
g++ -std=c++11 -O2 -o orderBook_bench.exe orderBook_bench.cpp orderBook_core.o
g++ -std=c++11 -O2 -o orderBook_stress.exe orderBook_stress.cpp orderBook_core.o
```

## Usage

Run the interactive simulator:

```bash
./orderBook.exe
```

| Key | Action |
|-----|--------|
| `1` | Switch to AAPL |
| `2` | Switch to MSFT |
| `3` | Switch to BTCUSD |
| `B` | Place a buy order (GTC, 10 qty) |
| `S` | Place a sell order (GTC, 10 qty) |
| `C` | Simulate cancel (IOC at best ask) |
| `Q` | Quit and print final summary |

The display refreshes every 500 ms. Each symbol has its own book and trade tape; background threads keep activity going.

## Project layout

| File | Description |
|------|-------------|
| `orderBook.cpp` | Interactive multi-symbol simulator (display + input + sim threads) |
| `orderBook_core.hpp` / `orderBook_core.cpp` | Reusable order book core for bench/stress |
| `orderBook_bench.cpp` | Latency/throughput benchmarks |
| `orderBook_stress.cpp` | Stress test and system usage logging |
| `orderBook.hpp` | Alternate API (legacy/experimental) |
| `Latency_Analysis.ipynb` / `System_Analysis.ipynb` | Jupyter notebooks for analysis |

## License

Use and modify as you like.
