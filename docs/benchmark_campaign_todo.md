# Benchmark Campaign — TODO

CLI-driven, dataset-scale reachability study: flat BRICK (BFS + closure), H-BRICK, CSR/graph baselines, and Kleene/Warshall closure variants. **Status: not started.**

**Goal:** reproducible maps, precise preprocess/query/memory numbers, hard memory caps, disk artifacts (data + gallery), and fair comparison across methods on the same query workload.

---

## 0. Reuse before building new code

- [ ] Extend `ReachabilityBenchmarkJob` / `ReachabilityBenchmarkConfig` and `reachability_benchmark_format` for CLI output rather than duplicating benchmark logic from the dataset browser.
- [ ] Reuse `tools::Recipe` (JSON) for orientation parameters: `RandomAsymmetric`, `AcyclicEastSouth`, `BidirectionalAll`, `GradientFlow`, seeds, and gradient fields.
- [ ] Reuse `tests/support/maze_generator` (`generatePerfectMaze`, `generateMazeWithExtraPassages`) for procedural grids with controlled connectedness (extra openings = more cycles).
- [ ] Reuse `tests/support/movingai_map_catalog` and recipe-based integration tests as a second dataset source alongside generated mazes.
- [ ] Reuse `reachability_oracle` / BFS checks for post-run correctness on sampled pairs (already wired in `ReachabilityBenchmarkConfig::correctness_check_count`).
- [ ] Reuse `tools/dataset_browser/map_render` pixel paths (or extract to `hbrick_viz`) for gallery PNG/SVG export without linking the full GUI.
- [ ] Reuse `currentProcessRssBytes()` (`process_memory.cpp`) to sample peak RSS during preprocess, not only ledger-estimated index bytes.
- [ ] Reuse existing micro-benchmarks (`kleene_closure_benchmark`, `port_closure_benchmark`) as optional preprocess-only tracks, separate from end-to-end query campaigns.

---

## 1. Campaign layout and output schema

- [ ] Define a standard output directory per campaign: `manifest.csv`, `results.csv`, `summary.md`, `gallery/`, `recipes/`, `logs/`.
- [ ] Version the result schema (`campaign_schema_version`) so old CSV rows remain parseable after adding columns.
- [ ] Record campaign metadata in every run: git commit hash, build type, compiler, CPU model, thread counts, and timestamp.
- [ ] Write one row per (map, method, parameter tuple); include `BaselineStatus` (`Completed`, `SkippedByPolicy`, `Failed`) so failures are not silently dropped.
- [ ] Generate both `csv` (analysis) and `md` (human summary) from one internal `BaselineBenchmarkMetrics` struct.
- [ ] Store query workload separately from map generation: `pair_seed`, `query_count`, `warmup_queries`, and hash of the pair list for cross-run comparison.

---

## 2. Reproducibility and tests (do early)

- [ ] Document the seeding contract: which RNG seeds control maze carving, extra openings, orientation, and query pairs, and in what order they are consumed.
- [ ] Add tests: fixed generator + recipe parameters → identical `MazeLayout`, identical CSR `(V, E)`, identical orientation edge set, identical gallery image hash.
- [ ] Add tests for `tools::Recipe` round-trip (save/load JSON) so CLI and GUI produce the same directed graph from the same recipe file.
- [ ] Add smoke test for the CLI campaign driver on one tiny map (one BRICK config, one H-BRICK config, one CSR baseline) with golden summary rows.
- [ ] Pin environment notes in docs: monotonic timers, whether hyperthreading/turbo affects comparability, and recommended `taskset` / thread limits for published numbers.

---

## 3. Dataset generation and manifest

- [ ] Add a CLI that generates maze datasets from parameter grids: logical size, `extra_openings` (connectedness), orientation mode, and asymmetric probabilities.
- [ ] Support MovingAI maps from disk as inputs (passability policy + recipe), not only procedural mazes.
- [ ] Emit `manifest.csv` (or `.md`) per campaign: map id, generator type, all parameters, seeds, paths to layout/CSR/recipe/gallery image, and characterization stats (see §4).
- [ ] Support batch generation independent of benchmarking so maps can be reviewed in the gallery before spending hours on closure builds.

---

## 4. Map characterization (manifest + results columns)

Precompute once per map and copy into every benchmark row:

- [ ] Passable cell count, grid width/height, directed vertex count `V`, directed edge count `E`.
- [ ] Undirected connected component count and largest undirected component size (on passable cells).
- [ ] Directed SCC count `C`, largest SCC size `S_max`, condensation DAG vertex/edge counts.
- [ ] Optional: `ReachabilityDensityEstimator` sample (global reachability fraction) for large maps where exact density is expensive.
- [ ] Recipe/orientation fields: mode, `p_one_way`, `p_bidirectional`, gradient angle, `p_against_gradient`.
- [ ] For BRICK runs: record nominal tile size, partial-edge-tile flag, port count `P`, and largest undirected port component `n_max`.

---

## 5. Maze gallery (reoriented images)

- [ ] Export gallery images to disk (PNG and/or SVG) using the same visual language as the dataset browser (terrain, passability, orientation overlay where applicable).
- [ ] Link `gallery_image_path` in the manifest; use deterministic filenames derived from map id + recipe seed.
- [ ] Add a test that gallery bytes are stable for fixed inputs (or hash-stable within a documented renderer version).

---

## 6. CLI benchmark runner (no GUI)

- [ ] Build `hbrick_benchmark_campaign` (or similar) CLI: load manifest → run methods → append results.
- [ ] Support `--resume` / skip-completed rows so multi-day sweeps can continue after interruption.
- [ ] Support `--methods`, `--maps`, and `--configs` filters for partial reruns.
- [ ] Log progress to stderr and per-campaign `logs/` with enough context to debug a single failing row.

### 6a. Flat BRICK sweeps

- [ ] Sweep tile size, memory cap, Kleene parallel options (`kleene_use_parallel`, thread count), and closure early-stop policy.
- [ ] Run both **BRICK-Search** and **BRICK-Closure** on each map and parameter tuple.

### 6b. H-BRICK sweeps

- [ ] Sweep base tile size, `group_size`, `max_depth`, and memory cap.
- [ ] H-BRICK is **not** in `ReachabilityBenchmarkConfig::defaultMethodList()` today — include it explicitly in the campaign method list.

### 6c. CSR / graph baselines (same maps, same query pairs)

- [ ] CsrBfs, CsrDfs, Grail, TwoHop, SccDagSearch, SccDagClosure, FullClosure on every map.
- [ ] Use **CsrBfs** as the reference for speedup columns (consistent with existing `ReachabilityBenchmarkJob`).

---

## 7. Correctness on every campaign row

- [ ] Run `correctness_check_count` random pairs per method against BFS oracle (or existing harness) and record mismatch count in results.
- [ ] For closure methods, optionally spot-check truncated Kleene vs Warshall on port/interface matrices for small `P` or `Γ` only (too expensive at scale).
- [ ] Never report throughput for rows where correctness checks failed unless flagged `correctness_failed=true`.

---

## 8. Memory accounting and hard limits

- [ ] Pre-flight estimate: refuse to start a case when projected `BitMatrix` / CSR storage exceeds `max_memory_bytes` (use `ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes`, tile preflight helpers).
- [ ] Record ledger-charged index bytes, `indexStorageBytes()`, closure scratch bytes, and peak RSS samples at end of preprocess.
- [ ] Add debug assertions (or test-only checks) that charged bytes match `BitMatrix::memoryBytes()` + CSR footprint for built indexes.
- [ ] Record `skipped_by_policy` with the reason (memory, tile size, empty ports, etc.) in the results row.

---

## 9. Timing and throughput (high precision)

- [ ] Use `steady_clock` / `BenchTimer` for preprocess, warmup, and timed query phases; document timer source in the schema.
- [ ] Report preprocess wall time with phase breakdown where available (index build, adjacency fill, Kleene/Warshall rounds, H-BRICK hierarchy levels).
- [ ] Report query stats: count, mean, median, min, max, p90/p95; **queries per second** from timed batch only (exclude warmup and correctness checks).
- [ ] Report **end-to-end** time (preprocess + all timed queries) and **amortized QPS** at the campaign’s `query_count` so BRICK/H-BRICK preprocess cost is visible.
- [ ] Record `kleene_rounds_scheduled` vs `kleene_rounds_effective` (early fixpoint) when exposed by the baseline.

---

## 10. Kleene / Warshall / representation variant matrix

Closure ladder to benchmark explicitly (not only the production default):

| ID | Variant |
|----|---------|
| K0 | Powers of \(A\) up to \(A^{N-1}\) |
| K1 | \((I+A)\) squaring, \(\lceil\log_2 N\rceil\) cap (largest WCC) |
| K2 | K1 + SCC-bounded round count (\(\min\) with \(\lceil\log_2(C+S_{\max})\rceil\)) |
| K3 | SCC-compressed \((I+A)\): Kleene on \(C\times C\), expand to \(V\times V\) |
| W0 | Warshall on full reflexive matrix (oracle) |
| W1 | Warshall on condensation DAG only (`SccDagClosure` storage model) |

- [ ] Expose each variant as a named benchmark mode (flags or config enum), including CSR-built vs bit-matrix-built adjacency where both exist.
- [ ] Correctness: each Kleene variant vs W0 on a sampled subset; report max matrix dimension where full Warshall is feasible.
- [ ] **Separate experiment track** for K3 vs `SccDagClosure` (W1): comparable preprocess cost, different storage/query models — do not mix in the main leaderboard without labeling.
- [ ] Document production default: K3 with fallback to K2/K1 when \(C = V\).

---

## 11. Analysis and reporting

- [ ] Produce campaign `summary.md`: tables of winners per map class (sparse, cyclic, large SCC count, etc.).
- [ ] Plot-friendly `results.csv` columns for preprocess time, index MB, QPS, speedup vs CsrBfs, and map characterization fields.
- [ ] Optional: export JSON Lines for streaming ingestion; optional notebook or script templates for pandas/R (out of repo is fine).
- [ ] Tag rows with `map_class` buckets (e.g. `perfect_maze`, `cyclic_maze`, `movingai`, `random_asymmetric`) for grouped analysis.

---

## 12. CI and regression

- [ ] PR smoke: 1–2 tiny maps, 2–3 methods, &lt;60s total, verifies CLI + oracle + CSV write.
- [ ] Nightly or manual full campaign job (document how to run, do not block PR CI).
- [ ] Keep a small **golden** `results.csv` snippet or checksum test for schema stability.

---

## 13. Open questions

- [ ] Primary storage for large campaigns: one big `results.csv` or one file per (map, method) for easier resume?
- [ ] Global campaign memory budget vs per-case `max_memory_bytes`?
- [ ] Include `ReachabilityDensity` in manifest for all maps or only when `V` is below a threshold?
- [ ] Should gallery include orientation edge overlay, SCC coloring, or port/tile boundaries for BRICK debugging?
- [ ] Publish separate leaderboards for **index MB** vs **QPS** vs **end-to-end** (preprocess amortized)?

---

## Reference — baseline inventory

| Category | Methods |
|----------|---------|
| Grid / BRICK | BRICK-Search, BRICK-Closure, H-BRICK |
| CSR / graph | CsrBfs, CsrDfs, Grail, TwoHop, SccDagSearch, SccDagClosure, FullClosure |

**Production Kleene (BRICK/H-BRICK preprocess):** `transitiveClosureKleeneSccCompressedInPlace` → K3, fallback to vertex-level K2/K1 when \(C = V\).

**Existing tests (partial reproducibility today):** `test_maze_generator`, `test_recipe_reachability`, `test_movingai_reachability`, `test_brick_reachability`, `test_hbrick_reachability` — extend for full manifest + gallery determinism.
