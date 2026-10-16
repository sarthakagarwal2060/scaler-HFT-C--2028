# Capstone: Low-Latency Order Book

- **email**: sarthak.24bcs10149@sst.scaler.com
- **roll_no**: 24BCS10149

A single-instrument order book (orders just rest, no matching), built **three different ways**
and benchmarked + profiled with `perf`.

- **A** → `std::map` (balanced tree)
- **B** → `std::priority_queue` + lazy deletion
- **C** → my own design: price-indexed flat arrays

---

## Build

One g++ command:

```bash
g++ -O2 -std=c++17 bench.cpp impl_a_map.cpp impl_b_pqueue.cpp impl_c_array.cpp -o bench
```

Or with CMake:

```bash
mkdir build && cd build
cmake ..
make
# the bench binary is then in build/
```

## Run

```bash
./bench --verify      # check all 3 give identical results
./bench --impl A      # benchmark A  (also B, C)
```

## Files

| file | what it is |
|------|-----------|
| `orderbook.h`                      | API structs (`Side`, `Level`, `BBO`) + shared `IOrderBook` interface |
| `impl_a_map.h` / `.cpp`            | Implementation A (`std::map`) |
| `impl_b_pqueue.h` / `.cpp`         | Implementation B (`priority_queue` + lazy deletion) |
| `impl_c_array.h` / `.cpp`          | Implementation C (flat arrays, my design) |
| `bench.cpp`                        | the one benchmark + `--verify` |
| `perf_results/`                    | saved `perf stat` / `perf report` output |

Each implementation is split into a header (class declaration) and a `.cpp` (method definitions);
`bench.cpp` includes the three headers and all four `.cpp` files are compiled together.

## Interface changes

I kept the function signatures the same (still `double price`). The only change: **prices are
stored as integer ticks** internally (`tick = price * 100`, so `100.01 → 10001`). This avoids
float compare issues and lets C use the tick as an array index. Bands as ticks: bids `5000..9999`,
asks `10001..15000` = 10,000 levels.

---

## The three designs

### A — `std::map`
- **Orders:** `unordered_map<id, order>`.
- **Levels:** `map<tick, qty>` (bids reversed so best bid is `begin()`).
- **Best price:** `begin()` of the map → O(1).
- **Cancel:** find id in hash, fix the level in the tree.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(log n) | O(log n) | O(1) | O(depth) | O(depth) |

### B — `priority_queue` + lazy deletion
- **Orders:** `unordered_map<id, order>`.
- **Levels:** `unordered_map<tick, qty>` + a heap of ticks per side.
- **Best price:** heap `top()`, after popping any dead (qty 0) ticks.
- **Cancel:** just lower the qty, leave the heap alone (lazy). Dead ticks get popped later.
- Weak spot: `get_bids/asks` must copy the whole heap and pop it.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(log n) | O(1) | O(log n)* | O(n log n) | O(n log n) |

\* amortized (popping dead tops)

### C — flat arrays (my design)
Only 10,000 prices exist, so I use the tick as an index — no searching.
- **Orders:** `vector` indexed **directly by id** (ids are dense → no hashing).
- **Levels:** `vector<long>` of 5000 per side, index = `tick − band_min`.
- **Best price:** I keep a cursor to the best level → O(1). On cancel that empties the best level, I
  walk to the next one over contiguous memory.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(1) | O(1)* | O(1) | O(L)† | O(L)† |

\* O(L) worst if it has to rescan for the next best level  †usually tiny

---

## Correctness

```
Running correctness test...

Implementation A: PASS  (reference)
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

All three also give the same final BBO (`Bid 99.99 x 2722`, `Ask 100.01 x 2525`) and the same
checksum `843022881`.

---

## Results

Machine: Intel **i5-13500H**, Pop!_OS, g++ 13.3, `-O2`. On this hybrid CPU the work runs on the
P-cores, so the `cpu_core` counters below are the real ones (`cpu_atom`/E-core rows are ~0 and
noisy). `./bench --impl X` runs the workload 5× internally, so the perf counts are ~5× one run —
same for all three, so the comparison is fair. Raw output in `perf_results/`.

| Metric | A (map) | B (pq) | C (array) |
|--------|--------:|-------:|----------:|
| Total time (ms) | 193.3 | 141.0 | **52.8** |
| ns / add     | 165 | 87 | **26** |
| ns / cancel  | 271 | 177 | **58** |
| ns / BBO     | 21 | 34 | **19** |
| IPC          | 0.96 | 1.38 | **1.88** |
| Branch miss % | **5.03** | 1.06 | 1.57 |
| L1-dcache misses | 65 M | **85 M** | 5.3 M |
| Cache misses | 20 M | 17 M | **3.0 M** |
| Page faults  | 18,297 | **28,776** | 23,183 |

(Bold = the extreme value for that row.)

---

## Analysis

**Which is fastest?**
C wins every operation (add, cancel, BBO) and is ~4× faster overall than A. It just does array
indexing — no tree balancing, no hashing, no heap. A is slowest at cancel because it does a hash
lookup *and* a tree erase. B's cancel is O(1) but still pays two hash lookups.

> Note: `perf record` shows ~47% of C's time is the `clock_gettime` calls I wrap around each op.
> C's ops are so fast the timer dominates, so its real per-op cost is even lower than shown. That's
> also why A's and C's BBO look almost equal (~19 ns) — both are near the timer floor.

**Branch prediction — who misses most?**
A (map), 5% misses, lowest IPC. Walking a red-black tree is a chain of compare-then-go-left-or-right
branches, and which way you go depends on the data, so the CPU can't predict them → pipeline stalls.
C's code is straight-line array math with predictable branches → nearly 2× the IPC.

**Cache behaviour — who misses most?**
C has the fewest by far. It reads **contiguous** arrays, so neighbours share cache lines and the
prefetcher helps. A scatters tree/hash nodes all over the heap → every lookup is a pointer chase
that misses cache. B has the most L1 misses because it holds two hash maps *and* a heap that keeps
growing with dead entries → biggest, most scattered working set.

**Page faults — who causes most?**
B. It allocates the most growing, scattered memory (hash maps rehashing + heap growing from lazy
dead entries). C is next but for a good reason — a one-time up-front `reserve`. A has the fewest
because the live book stays small.

**Which one to deploy?**
**C.** Not just because it's fastest on average, but because it's **predictable**: straight-line
code + contiguous memory = tight, repeatable latency with no surprise spikes (HFT cares about the
tail, not the mean). Fixed, known memory. Best cache behaviour. The only downside is it *assumes*
the fixed price band and dense ids — but for this spec (one instrument, fixed tick) that's always
true. If the price range were unknown I'd pick the map instead for safety.

**What I'd try next**
- Integer ticks in the public API too (drop the `double`).
- Stop timing each op with `clock_gettime` (it's half of C's time) — time batches or use `rdtsc`.
- In C, replace the "walk to next best level" scan with a bitset + `ctz` for O(1) next-best.
- Object pool / pre-allocation for A and B (A spent lots of time in `free`).
- Custom hash for A/B's id lookup — the std hashtable `find` was their hottest function.
