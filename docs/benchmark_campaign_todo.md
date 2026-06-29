# Benchmark Campaign — TODO

CLI-driven, dataset-scale reachability study: flat BRICK (BFS + closure), H-BRICK, CSR/graph baselines, and Kleene/Warshall closure variants. **Status: sections 0–13 largely implemented** (see `docs/benchmark_campaign.md`, `docs/benchmark_campaign_seeding.md`, `docs/benchmark_campaign_kleene.md`, `docs/benchmark_campaign_decisions.md`).

**Goal:** reproducible maps, precise preprocess/query/memory numbers, hard memory caps, disk artifacts (data + gallery), and fair comparison across methods on the same query workload.

---

## 0. Reuse before building new code

- [x] Extend `ReachabilityBenchmarkJob` / `ReachabilityBenchmarkConfig` and `reachability_benchmark_format` for CLI output rather than duplicating benchmark logic from the dataset browser.
- [x] Reuse `tools::Recipe` (JSON) for orientation parameters: `RandomAsymmetric`, `AcyclicEastSouth`, `BidirectionalAll`, `GradientFlow`, seeds, and gradient fields.
- [x] Reuse `tests/support/maze_generator` (`generatePerfectMaze`, `generateMazeWithExtraPassages`) for procedural grids with controlled connectedness (extra openings = more cycles).
- [x] Reuse `tests/support/movingai_map_catalog` and recipe-based integration tests as a second dataset source alongside generated mazes.
- [x] Reuse `reachability_oracle` / BFS checks for post-run correctness on sampled pairs (already wired in `ReachabilityBenchmarkConfig::correctness_check_count`).
- [x] Reuse `tools/dataset_browser/map_render` pixel paths (or extract to `hbrick_viz`) for gallery PNG/SVG export without linking the full GUI. *(Headless passability PPM in `benchmark_campaign_gallery`; orientation overlay still future.)*
- [x] Reuse `currentProcessRssBytes()` (`process_memory.cpp`) to sample peak RSS during preprocess, not only ledger-estimated index bytes.
- [x] Reuse existing micro-benchmarks (`kleene_closure_benchmark`, `port_closure_benchmark`) as optional preprocess-only tracks, separate from end-to-end query campaigns. (`kleene-micro` CLI wrapper; see `docs/benchmark_campaign_kleene.md`.)

---

## 1. Campaign layout and output schema

- [x] Define a standard output directory per campaign: `manifest.csv`, `results.csv`, `summary.md`, `gallery/`, `recipes/`, `logs/`.
- [x] Version the result schema (`campaign_schema_version`) so old CSV rows remain parseable after adding columns.
- [x] Record campaign metadata in every run: git commit hash, build type, compiler, CPU model, thread counts, and timestamp.
- [x] Write one row per (map, method, parameter tuple); include `BaselineStatus` (`Completed`, `SkippedByPolicy`, `Failed`) so failures are not silently dropped.
- [x] Generate both `csv` (analysis) and `md` (human summary) from one internal `BaselineBenchmarkMetrics` struct.
- [x] Store query workload separately from map generation: `pair_seed`, `query_count`, `warmup_queries`, and hash of the pair list for cross-run comparison.

---

## 2. Reproducibility and tests (do early)

- [x] Document the seeding contract: which RNG seeds control maze carving, extra openings, orientation, and query pairs, and in what order they are consumed. (`docs/benchmark_campaign_seeding.md`)
- [x] Add tests: fixed generator + recipe parameters → identical `MazeLayout`, identical CSR `(V, E)`, identical orientation edge set, identical gallery image hash.
- [ ] Add tests for `tools::Recipe` round-trip (save/load JSON) so CLI and GUI produce the same directed graph from the same recipe file. *(Covered in `test_recipe_reachability`; campaign-specific round-trip optional.)*
- [x] Add smoke test for the CLI campaign driver on one tiny map (one BRICK config, one H-BRICK config, one CSR baseline) with golden summary rows. (`smoke` command + unit tests; golden CSV header test.)
- [x] Pin environment notes in docs: monotonic timers, whether hyperthreading/turbo affects comparability, and recommended `taskset` / thread limits for published numbers.

---

## 3. Dataset generation and manifest

- [x] Add a CLI that generates maze datasets from parameter grids: logical size, `extra_openings` (connectedness), orientation mode, and asymmetric probabilities. (`generate` command)
- [x] Support MovingAI maps from disk as inputs (passability policy + recipe), not only procedural mazes. (`import-movingai` command)
- [x] Emit `manifest.csv` (or `.md`) per campaign: map id, generator type, all parameters, seeds, paths to layout/CSR/recipe/gallery image, and characterization stats (see §4). *(Schema v2 manifest columns.)*
- [x] Support batch generation independent of benchmarking so maps can be reviewed in the gallery before spending hours on closure builds.

---

## 4. Map characterization (manifest + results columns)

Precompute once per map and copy into every benchmark row:

- [x] Passable cell count, grid width/height, directed vertex count `V`, directed edge count `E`.
- [x] Undirected connected component count and largest undirected component size (on passable cells).
- [x] Directed SCC count `C`, largest SCC size `S_max`, condensation DAG vertex/edge counts.
- [ ] Optional: `ReachabilityDensityEstimator` sample (global reachability fraction) for large maps where exact density is expensive.
- [x] Recipe/orientation fields: mode, `p_one_way`, `p_bidirectional`, gradient angle, `p_against_gradient`.
- [ ] For BRICK runs: record nominal tile size, partial-edge-tile flag, port count `P`, and largest undirected port component `n_max`.

---

## 5. Maze gallery (reoriented images)

- [x] Export gallery images to disk (PNG and/or SVG) using the same visual language as the dataset browser (terrain, passability, orientation overlay where applicable). *(Passability PPM only for now.)*
- [x] Link `gallery_image_path` in the manifest; use deterministic filenames derived from map id + recipe seed.
- [x] Add a test that gallery bytes are stable for fixed inputs (or hash-stable within a documented renderer version).

---

## 6. CLI benchmark runner (no GUI)

- [x] Build `hbrick_benchmark_campaign` (or similar) CLI: load manifest → run methods → append results.
- [x] Support `--resume` / skip-completed rows so multi-day sweeps can continue after interruption.
- [x] Support `--methods`, `--maps`, and `--configs` filters for partial reruns.
- [x] Log progress to stderr and per-campaign `logs/` with enough context to debug a single failing row.

### 6a. Flat BRICK sweeps

- [x] Sweep tile size, memory cap, Kleene parallel options (`kleene_use_parallel`, thread count), and closure early-stop policy. (`--configs brick`, `brick-kleene`; memory/early-stop columns in results)
- [x] Run both **BRICK-Search** and **BRICK-Closure** on each map and parameter tuple. (`--preset brick` / `all`)

### 6b. H-BRICK sweeps

- [x] Sweep base tile size, `group_size`, `max_depth`, and memory cap. (`--configs hbrick`)
- [x] H-BRICK is **not** in `ReachabilityBenchmarkConfig::defaultMethodList()` today — include it explicitly in the campaign method list. (`--preset all` / `brick` / `smoke`)

### 6c. CSR / graph baselines (same maps, same query pairs)

- [x] CsrBfs, CsrDfs, Grail, TwoHop, SccDagSearch, SccDagClosure, FullClosure on every map. (`--preset csr` or `all`)
- [x] Use **CsrBfs** as the reference for speedup columns (consistent with existing `ReachabilityBenchmarkJob`).

---

## 7. Correctness on every campaign row

- [x] Run `correctness_check_count` random pairs per method against BFS oracle (or existing harness) and record mismatch count in results.
- [ ] For closure methods, optionally spot-check truncated Kleene vs Warshall on port/interface matrices for small `P` or `Γ` only (too expensive at scale).
- [x] Never report throughput for rows where correctness checks failed unless flagged `correctness_failed=true`.

---

## 8. Memory accounting and hard limits

- [x] Pre-flight estimate: refuse to start a case when projected `BitMatrix` / CSR storage exceeds `max_memory_bytes` (use `ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes`, tile preflight helpers).
- [x] Record ledger-charged index bytes, `indexStorageBytes()`, closure scratch bytes, and peak RSS samples at end of preprocess.
- [ ] Add debug assertions (or test-only checks) that charged bytes match `BitMatrix::memoryBytes()` + CSR footprint for built indexes.
- [x] Record `skipped_by_policy` with the reason (memory, tile size, empty ports, etc.) in the results row.

---

## 9. Timing and throughput (high precision)

- [x] Use `steady_clock` / `BenchTimer` for preprocess, warmup, and timed query phases; document timer source in the schema.
- [ ] Report preprocess wall time with phase breakdown where available (index build, adjacency fill, Kleene/Warshall rounds, H-BRICK hierarchy levels).
- [x] Report query stats: count, mean, median, min, max, p90/p95; **queries per second** from timed batch only (exclude warmup and correctness checks).
- [x] Report **end-to-end** time (preprocess + all timed queries) and **amortized QPS** at the campaign’s `query_count` so BRICK/H-BRICK preprocess cost is visible.
- [x] Record `kleene_rounds_scheduled` vs `kleene_rounds_effective` (early fixpoint) when exposed by the baseline.

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

- [x] Expose each variant as a named benchmark mode (flags or config enum), including CSR-built vs bit-matrix-built adjacency where both exist. *(W0/W1 via `--preset kleene-oracle`; K0–K2 in `kleene_closure_benchmark`; K3 in production BRICK.)*
- [ ] Correctness: each Kleene variant vs W0 on a sampled subset; report max matrix dimension where full Warshall is feasible.
- [x] **Separate experiment track** for K3 vs `SccDagClosure` (W1): comparable preprocess cost, different storage/query models — do not mix in the main leaderboard without labeling.
- [x] Document production default: K3 with fallback to K2/K1 when \(C = V\). (`docs/benchmark_campaign_kleene.md`)

---

## 11. Analysis and reporting

- [x] Produce campaign `summary.md`: tables of winners per map class (sparse, cyclic, large SCC count, etc.).
- [x] Plot-friendly `results.csv` columns for preprocess time, index MB, QPS, speedup vs CsrBfs, and map characterization fields.
- [ ] Optional: export JSON Lines for streaming ingestion; optional notebook or script templates for pandas/R (out of repo is fine).
- [x] Tag rows with `map_class` buckets (e.g. `perfect_maze`, `cyclic_maze`, `movingai`, `random_asymmetric`) for grouped analysis.

---

## 12. CI and regression

- [x] PR smoke: 1–2 tiny maps, 2–3 methods, &lt;60s total, verifies CLI + oracle + CSV write. (`ctest -R BenchmarkCampaign`)
- [ ] Nightly or manual full campaign job (document how to run, do not block PR CI).
- [x] Keep a small **golden** `results.csv` snippet or checksum test for schema stability. (`ResultsCsvHeaderIsGolden` unit test)

---

## 13. Open questions

- [x] Primary storage for large campaigns: one big `results.csv` or one file per (map, method) for easier resume? *(One `results.csv`; see `docs/benchmark_campaign_decisions.md`)*
- [x] Global campaign memory budget vs per-case `max_memory_bytes`? *(Per-case cap for now.)*
- [x] Include `ReachabilityDensity` in manifest for all maps or only when `V` is below a threshold? *(Not by default.)*
- [x] Should gallery include orientation edge overlay, SCC coloring, or port/tile boundaries for BRICK debugging? *(PPM passability only in v1.)*
- [x] Publish separate leaderboards for **index MB** vs **QPS** vs **end-to-end** (preprocess amortized)? *(Documented views from same CSV.)*

---

## Reference — baseline inventory

| Category | Methods |
|----------|---------|
| Grid / BRICK | BRICK-Search, BRICK-Closure, H-BRICK |
| CSR / graph | CsrBfs, CsrDfs, Grail, TwoHop, SccDagSearch, SccDagClosure, FullClosure |

**Production Kleene (BRICK/H-BRICK preprocess):** `transitiveClosureKleeneSccCompressedInPlace` → K3, fallback to vertex-level K2/K1 when \(C = V\).

**Existing tests (partial reproducibility today):** `test_maze_generator`, `test_recipe_reachability`, `test_movingai_reachability`, `test_brick_reachability`, `test_hbrick_reachability` — extend for full manifest + gallery determinism.
