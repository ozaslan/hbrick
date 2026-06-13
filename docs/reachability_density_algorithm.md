# Reachability density estimation: algorithm and implementation

Technical description of how **hbrick** estimates the *reachable-pair density* of a directed graph. The procedure is implemented in `hbrick_graph` and consumed by the dataset browser and unit tests. All behaviour below matches the current source code.

**Primary implementation**

| Component | Path |
|-----------|------|
| Public API | [`include/hbrick/graph/reachability_density.hpp`](../include/hbrick/graph/reachability_density.hpp) |
| Implementation | [`src/graph/reachability_density.cpp`](../src/graph/reachability_density.cpp) |
| Per-source BFS | [`include/hbrick/graph/bfs.hpp`](../include/hbrick/graph/bfs.hpp), [`src/graph/bfs.cpp`](../src/graph/bfs.cpp) |
| Traversal workspace | [`include/hbrick/graph/graph_search_scratch.hpp`](../include/hbrick/graph/graph_search_scratch.hpp) |
| Unit tests | [`tests/unit/test_reachability_density.cpp`](../tests/unit/test_reachability_density.cpp) |

**User guide (shorter):** [Reachability density](reachability_density.md)

---

## 1. Estimand

Let \(G = (V, E)\) be a directed graph in CSR form ([`CsrGraph`](../include/hbrick/graph/csr_graph.hpp)) and let \(U \subseteq V\) be a **universe** of vertex indices supplied by the caller (e.g. passable grid cells).

For a source \(s \in U\), define the **per-source reachable fraction**

\[
f(s) = \frac{\bigl|\{ v \in V : s \leadsto v \}\bigr|}{|U|}
\]

where \(s \leadsto v\) denotes forward reachability along directed arcs. The numerator is computed by a full forward BFS from \(s\) over the entire vertex set \(V\) (not restricted to \(U\)); the denominator is always \(|U|\).

The library reports the **sample mean**

\[
\hat{\rho} = \frac{1}{N} \sum_{i=1}^{N} f(s_i)
\]

over \(N\) distinct sources \(s_1, \ldots, s_N\) drawn from a prefix of a shuffled copy of \(U\) (Section 3). This is an unbiased Monte Carlo estimator of the average ordered-pair reachability fraction when the source is uniform over \(U\).

The output structure is [`ReachabilityDensityEstimate`](../include/hbrick/graph/reachability_density.hpp): fields `density` (\(\hat{\rho}\)), `std_error` (standard error of the sample mean), and `samples` (\(N\)).

---

## 2. Per-source measurement: `Bfs::reachableCount`

Each sample invokes [`Bfs::reachableCount`](../include/hbrick/graph/bfs.hpp), which performs a standard queue-based BFS using a preallocated [`GraphSearchScratch`](../include/hbrick/graph/graph_search_scratch.hpp):

```50:85:src/graph/bfs.cpp
uint32_t Bfs::reachableCount(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (source >= num_vertices) {
        return 0U;
    }

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);
    uint32_t count = 1U;

    std::size_t head = 0;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head];
        ++head;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
            ++count;
        }
    }

    return count;
}
```

The returned count includes the source. The per-source fraction stored by the estimator is `reached / |U|` (see `runSampleBatch` below).

---

## 3. Source selection: partial Fisher–Yates shuffle

At [`ReachabilityDensityEstimator::begin`](../include/hbrick/graph/reachability_density.hpp), the universe is copied into `universe_` and the first \(k = \min(\texttt{max\_samples}, |U|)\) entries are partially shuffled by `partialShuffleUniverse` ([`reachability_density.cpp`](../src/graph/reachability_density.cpp), lines 63–84). This is the partial Fisher–Yates algorithm: for each index \(i \in \{0, \ldots, k-1\}\), swap `values[i]` with `values[j]` where \(j\) is uniform in \(\{i, \ldots, |U|-1\}\).

**Consequences (as implemented):**

- Sources \(s_i = \texttt{universe\_[}i\texttt{]}\) for \(i = 0, \ldots, N-1\) are **pairwise distinct** within one estimate.
- If \(|U| \le \texttt{max\_samples}\), then \(N = |U|\) (exhaustive mode): every universe vertex is used exactly once.
- If \(|U| > \texttt{max\_samples}\), then \(N = \texttt{max\_samples}\): a simple random sample of distinct universe vertices without replacement.
- Order is fixed at `begin()` and determined by `rng_seed` ([`ReachabilityDensityConfig`](../include/hbrick/graph/reachability_density.hpp)).

Subsequent samples always take the next index in order: `universe_[completed_]`, `universe_[completed_ + 1]`, …

---

## 4. Configuration

[`ReachabilityDensityConfig`](../include/hbrick/graph/reachability_density.hpp) controls sampling:

| Field | Role |
|-------|------|
| `max_samples` | Upper bound on distinct sources \(N\) |
| `sample_mode` | `FixedSamples` or `AutoStopWhenStable` (Section 7) |
| `rng_seed` | Seed for the partial shuffle |
| `num_threads` | `1` = serial; `0` = `std::thread::hardware_concurrency()`; `N` = fixed worker count (capped at 64) |

Thread count is resolved in `resolveEffectiveThreads` ([`reachability_density.cpp`](../src/graph/reachability_density.cpp), lines 86–97) and stored in `effective_threads_`.

---

## 5. Single-threaded execution

### 5.1 Entry points

| Function | File | Behaviour |
|----------|------|-----------|
| `step(graph)` | `reachability_density.cpp:215–217` | One BFS sample per call |
| `estimate(graph, universe, config)` | `reachability_density.cpp:253–267` | Blocking loop; uses `step` when `effective_threads_ == 1` |

`step` delegates to `runSampleBatch(graph, 1)`.

### 5.2 Serial branch of `runSampleBatch`

When `effective_threads_ <= 1` or the batch size is 1, samples run sequentially on the calling thread with a single `scratch_`:

```163:168:src/graph/reachability_density.cpp
    if (effective_threads_ <= 1U || count == 1U) {
        for (uint32_t offset = 0; offset < count; ++offset) {
            const uint32_t source = universe_[completed_ + offset];
            const uint32_t reached = Bfs::reachableCount(graph, source, scratch_);
            fractions[offset] = static_cast<double>(reached) / universe_size_;
        }
```

After each batch, fractions are merged into running statistics (`sum_`, `sum_squares_`), and `completed_` is incremented (lines 195–199).

### 5.3 Pseudocode (serial)

```
begin(graph, universe, config):
    shuffle prefix of universe_
    completed ← 0

step(graph):
    s ← universe_[completed]
    f ← Bfs::reachableCount(graph, s, scratch_) / |U|
    update running mean accumulators with f
    completed ← completed + 1
    if stopping rule satisfied: finalize and return done
```

---

## 6. Parallel execution

Parallelism is confined to **cold-path analysis** in `ReachabilityDensityEstimator`. Individual `Bfs::reachableCount` calls remain single-threaded; concurrency arises from running multiple BFS traversals concurrently on distinct sources.

### 6.1 Entry points

| Function | File | Behaviour |
|----------|------|-----------|
| `stepParallel(graph, batch_size)` | `reachability_density.cpp:219–226` | Up to `batch_size` samples per call (default: `effective_threads_`) |
| `estimate(...)` | `reachability_density.cpp:260–261` | Uses `stepParallel` when `effective_threads_ > 1` |

### 6.2 Scratch pool allocation

At `begin`, if `effective_threads_ > 1`, the estimator allocates one `GraphSearchScratch` per worker:

```114:122:src/graph/reachability_density.cpp
    effective_threads_ = resolveEffectiveThreads(config_.num_threads);
    scratch_.resetForGraph(graph.numVertices());
    scratch_pool_.clear();
    if (effective_threads_ > 1U) {
        scratch_pool_.reserve(effective_threads_);
        for (uint32_t worker = 0; worker < effective_threads_; ++worker) {
            scratch_pool_.emplace_back(graph.numVertices());
        }
    }
```

The read-only `CsrGraph` is shared; scratch buffers are not.

### 6.3 Parallel branch of `runSampleBatch`

For `effective_threads_ > 1` and batch size `count > 1`, work is split into **waves** of at most `effective_threads_` concurrent `std::thread` workers:

```169:192:src/graph/reachability_density.cpp
    } else {
        uint32_t offset = 0U;
        while (offset < count) {
            const uint32_t wave = std::min(count - offset, effective_threads_);
            std::vector<std::thread> workers;
            workers.reserve(wave);
            for (uint32_t worker = 0; worker < wave; ++worker) {
                const uint32_t sample_index = completed_ + offset + worker;
                const uint32_t source = universe_[sample_index];
                workers.emplace_back([&, worker, source]() {
                    const uint32_t reached = Bfs::reachableCount(
                        graph,
                        source,
                        scratch_pool_[worker]
                    );
                    fractions[offset + worker] =
                        static_cast<double>(reached) / universe_size_;
                });
            }
            for (std::thread& worker : workers) {
                worker.join();
            }
            offset += wave;
        }
    }
```

**Source assignment invariant:** worker `w` in a wave uses `universe_[completed_ + offset + w]`. Indices are disjoint within the wave and across all prior samples, so no two parallel BFS runs share a start vertex.

**Synchronisation:** each wave is fork–join (`emplace_back` + `join`). Accumulators `sum_` and `sum_squares_` are updated on the main thread after the wave completes (lines 195–199).

### 6.4 Pseudocode (parallel)

```
begin(...):
    effective_threads ← resolve(num_threads)
    allocate scratch_pool_[0 .. effective_threads - 1]
    shuffle prefix of universe_

stepParallel(graph, batch_size):
    count ← min(batch_size, remaining samples)
    for offset in 0 .. count-1 in waves of size effective_threads:
        parallel for worker in 0 .. wave-1:
            s ← universe_[completed + offset + worker]
            fractions[offset + worker] ← reachableCount(graph, s, scratch_pool_[worker]) / |U|
        join all workers
    merge fractions into sum_, sum_squares_
    completed ← completed + count
    if stopping rule satisfied: finalize and return done
```

### 6.5 Serial–parallel equivalence

For identical `rng_seed`, `max_samples`, and universe, serial (`num_threads = 1`) and parallel (`num_threads > 1`) runs visit the same sources in the same order and compute the same per-source fractions. The sample mean is therefore identical; this is verified in `ParallelEstimateMatchesSerialForSameSeed` ([`test_reachability_density.cpp`](../tests/unit/test_reachability_density.cpp)).

---

## 7. Aggregation and uncertainty

After each batch, the estimator updates:

- `sum_` \(\mathrel{+}= \sum_i f_i\)
- `sum_squares_` \(\mathrel{+}= \sum_i f_i^2\)
- `completed_` \(\mathrel{+}= N_{\text{batch}}\)

The snapshot mean and standard error are computed in `computeSnapshot` ([`reachability_density.cpp`](../src/graph/reachability_density.cpp), lines 37–61):

\[
\hat{\rho} = \frac{\texttt{sum\_}}{\texttt{completed\_}}, \qquad
\widehat{\mathrm{SE}} = \sqrt{\frac{\widehat{\mathrm{Var}}(f)}{\texttt{completed\_}}}
\]

where \(\widehat{\mathrm{Var}}(f) = \texttt{sum\_squares\_}/n - \hat{\rho}^2\) (clamped at 0).

**Exact estimate:** when the job is exhaustive, not stopped early, and `completed_ >= source_count_`, `std_error` is set to `0` (lines 50–51).

**Finalise:** `finalize` writes the result to `ReachabilityDensityEstimate` via the same formula, with an additional `stopped_early` flag when auto-stop or manual `stop()` ends the job before the planned sample count.

---

## 8. Stopping rules

Controlled by [`ReachabilityDensitySampleMode`](../include/hbrick/graph/reachability_density.hpp):

### 8.1 `FixedSamples`

The job ends when `completed_ >= source_count_`, where `source_count_` is `max_samples` (or \(|U|\) in exhaustive mode). Implemented at the end of `runSampleBatch` (lines 207–210).

### 8.2 `AutoStopWhenStable`

`checkAutoConvergence` ([`reachability_density.cpp`](../src/graph/reachability_density.cpp), lines 295–329) may end the job early:

1. Requires `completed_ >= 64` (`kAutoMinSamples`).
2. Checks only when `completed_` is a multiple of 32 (`kAutoCheckInterval`).
3. Compares current \(\hat{\rho}\) and \(\widehat{\mathrm{SE}}\) to the previous checkpoint; both must change by less than a relative tolerance (`metricsStable`, lines 19–35).
4. Requires 3 consecutive stable checks (`kAutoStableRoundsRequired`).

`max_samples` remains a hard cap regardless of mode.

### 8.3 Manual `stop()`

[`stop()`](../include/hbrick/graph/reachability_density.hpp) finalises with whatever samples have completed (`stopped_early = true`). [`cancel()`](../include/hbrick/graph/reachability_density.hpp) discards the job without updating the stored result.

---

## 9. Incremental vs blocking API

| Mode | API | Use |
|------|-----|-----|
| Incremental | `begin` + repeated `step` / `stepParallel` | Dataset browser (one batch per UI frame, ~12 ms budget) |
| Blocking | `estimate` | Tests and batch tools |

Blocking `estimate` loops until `active_` is false:

```258:265:src/graph/reachability_density.cpp
    begin(graph, universe, config);
    while (active_) {
        if (effective_threads_ > 1U) {
            stepParallel(graph);
        } else {
            step(graph);
        }
    }
```

---

## 10. Dataset browser integration

The GUI does not implement estimation logic. [`tools/dataset_browser/orientation.cpp`](../tools/dataset_browser/orientation.cpp):

1. Builds the universe via `passableVertices(layout)` (passable `MazeLayout` cells).
2. Sets `config.num_threads = 0` when parallel sampling is enabled, else `1` (`densityConfig`, lines 73–82).
3. Calls `density_estimator.begin(...)` / `stepParallel(...)` through `beginDensityEstimate` and `stepDensityEstimateParallel` (lines 96–133).

The progress modal in [`browser_app.cpp`](../tools/dataset_browser/browser_app.cpp) invokes `stepDensityEstimateParallel` once per frame slice while the job is active.

---

## 11. Build dependency

Parallel execution requires linking the POSIX threads library:

```cmake
# src/graph/CMakeLists.txt
find_package(Threads REQUIRED)
target_link_libraries(hbrick_graph PUBLIC Threads::Threads)
```

---

## 12. Summary

| Aspect | Serial (`num_threads = 1`) | Parallel (`num_threads ≠ 1`) |
|--------|---------------------------|--------------------------------|
| BFS primitive | `Bfs::reachableCount` | Same |
| Scratch | One `scratch_` on caller thread | `scratch_pool_[worker]` per thread |
| Source assignment | `universe_[completed + offset]` | Same indices; disjoint per wave |
| Statistics | Main-thread `sum_`, `sum_squares_` | Updated after each join |
| Result | `ReachabilityDensityEstimate` | Identical for same seed and config |

The reachable-pair density is therefore the sample mean of forward BFS reachable-set sizes, normalised by \(|U|\), over distinct universe sources chosen once at job start; parallelisation is an implementation strategy for independent per-source BFS traversals, not a change to the estimand.
