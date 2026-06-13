# Reachability density

Sampled estimation of how **connected** a directed graph is in an ordered-pair sense: the average fraction of universe vertices forward-reachable from a randomly chosen source.

**API reference:** [`include/hbrick/graph/reachability_density.hpp`](../include/hbrick/graph/reachability_density.hpp)  
**Implementation:** [`src/graph/reachability_density.cpp`](../src/graph/reachability_density.cpp)  
**Tests:** [`tests/unit/test_reachability_density.cpp`](../tests/unit/test_reachability_density.cpp).  
**GUI:** [Dataset browser](dataset_browser.md) orientation editor (thin wrapper over the library estimator).

**Algorithm specification (academic):** [Reachability density — algorithm and implementation](reachability_density_algorithm.md)

---

## What it measures

Given a directed `CsrGraph` and a **universe** \(U\) of vertex indices (e.g. passable grid cells):

\[
f(s) = \frac{|\{ v : s \leadsto v \}|}{|U|}
\]

For each source \(s \in U\), run forward BFS, count reachable vertices (including \(s\)), divide by \(|U|\).

The **density** is the mean of \(f(s)\) over sampled sources:

\[
\text{density} = \frac{1}{N} \sum_{i=1}^{N} f(s_i)
\]

This estimates the **ordered reachable-pair fraction** over \(U\): how often a random ordered pair \((s, t)\) with \(s, t \in U\) has a directed path from \(s\) to \(t\).

| Value | Interpretation |
|-------|----------------|
| `1.0` | From typical sources, essentially all universe vertices are reachable |
| Near `0` | Highly fragmented — most pairs unreachable |

The result includes a **standard error** on the sample mean (displayed as ±2σ in the dataset browser). When every universe vertex is used exactly once without early stop, the estimate is **exact** (`std_error = 0`).

---

## Universe and distinct sources

The caller supplies `std::span<const uint32_t> universe`. In the dataset browser this is the list of passable `MazeLayout` vertices.

At `begin()`, the estimator copies the universe and runs a **partial Fisher–Yates shuffle** on the first `min(max_samples, |U|)` slots. Each BFS sample takes the next slot in order:

```text
universe_[0], universe_[1], …, universe_[N-1]
```

**No source is reused** within one estimate. Parallel workers in a batch take adjacent slots (`completed`, `completed+1`, …), so they never collide on the same start cell.

| Case | Sources used |
|------|----------------|
| `\|U\| ≤ max_samples` | Every universe vertex once (exhaustive) |
| `\|U\| > max_samples` | `max_samples` distinct vertices from the shuffled prefix |

---

## Configuration

[`ReachabilityDensityConfig`](../include/hbrick/graph/reachability_density.hpp):

| Field | Default | Meaning |
|-------|---------|---------|
| `max_samples` | `512` | Cap on distinct sources (or `\|U\|` when smaller) |
| `sample_mode` | `AutoStopWhenStable` | See stopping modes below |
| `rng_seed` | fixed constant | Reproducible shuffle and sample order |
| `num_threads` | `1` | `1` = serial; `0` = hardware concurrency; `N` = fixed worker count (max 64) |

### Stopping modes

**`FixedSamples`** — run exactly `max_samples` BFS passes (or all of \(U\) when exhaustive).

**`AutoStopWhenStable`** — stop early when density and σ stabilize:

- Minimum 64 samples before checking
- Check every 32 samples
- Stable when both metrics change less than tolerance for 3 consecutive checks
- `max_samples` remains a hard cap

Call `stop()` to accept a partial estimate; call `cancel()` to discard the job.

---

## Serial and parallel execution

### Serial

```cpp
ReachabilityDensityEstimator estimator;
estimator.begin(graph, universe, config);
while (estimator.active()) {
    estimator.step(graph);  // one BFS per call
}
ReachabilityDensityEstimate result = estimator.result();
```

Or blocking: `estimator.estimate(graph, universe, config)` (uses parallel batches when `num_threads != 1`).

### Parallel

```cpp
config.num_threads = 0;  // hardware concurrency
estimator.begin(graph, universe, config);
while (estimator.active()) {
    estimator.stepParallel(graph);  // up to num_threads distinct BFS per call
}
```

Each worker thread owns a dedicated [`GraphSearchScratch`](../include/hbrick/graph/graph_search_scratch.hpp). The graph is read-only and shared; scratch buffers are not.

Parallel and serial runs with the same `rng_seed` and `max_samples` produce **identical** density values (same sources, same order, commutative mean).

---

## Building blocks

| Component | Role |
|-----------|------|
| [`Bfs::reachableCount`](../include/hbrick/graph/bfs.hpp) | Forward BFS; returns reachable vertex count for one source |
| [`ReachabilityDensityEstimator`](../include/hbrick/graph/reachability_density.hpp) | Shuffle, sample scheduling, statistics, auto-stop |
| [`GraphSearchScratch`](../include/hbrick/graph/graph_search_scratch.hpp) | Per-thread BFS workspace (one per worker when parallel) |

Hot-path rules (no allocations inside `Bfs::reachableCount`) still apply. The estimator itself is **cold-path analysis**: it may allocate vectors, threads, and RNG state outside traversal.

---

## Dataset browser mapping

| UI control | Library setting |
|------------|-----------------|
| **Parallel sampling** (default on) | `num_threads = 0` |
| Parallel off | `num_threads = 1` |
| **Choose samples automatically** | `AutoStopWhenStable` |
| Auto off | `FixedSamples` |
| Sample count combo | `max_samples` |

The progress modal runs `stepParallel()` once per UI frame slice (~12 ms budget), so large maps stay responsive.

---

## Related references

| Resource | Location |
|----------|----------|
| Header | [`include/hbrick/graph/reachability_density.hpp`](../include/hbrick/graph/reachability_density.hpp) |
| Implementation | [`src/graph/reachability_density.cpp`](../src/graph/reachability_density.cpp) |
| Unit tests | [`tests/unit/test_reachability_density.cpp`](../tests/unit/test_reachability_density.cpp) |
| Scratch threading | [GraphSearchScratch design notes](graph_search_scratch.md) § Threading |
| Atlas entry | [Atlas](atlas.md) § hbrick_graph — algorithms |
