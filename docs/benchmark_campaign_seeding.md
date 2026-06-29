# Benchmark campaign seeding contract

Reproducibility depends on keeping generation seeds separate from query seeds.

## Procedural mazes

| Stage | Parameter | Consumed by |
|-------|-----------|-------------|
| Perfect maze carving | `carve_seed` | `test_support::generatePerfectMaze` (`MazeParams::seed`) |
| Extra openings | `opening_seed`, `extra_openings` | `test_support::generateMazeWithExtraPassages` |
| Directed orientation | `orientation_seed`, `orientation_mode`, `p_one_way`, `p_bidirectional`, gradient fields | `DirectedGridGraphBuilder::build` via `RandomAsymmetricParams` |
| Map index in a batch | `index` in `proceduralMapId` | Filename / `map_id` only (does not change layout when other fields match) |

Generation order in `generateProceduralCampaignMaps`:

1. Carve layout from `(logical_width, logical_height, carve_seed)`.
2. Add `extra_openings` walls removed using `opening_seed`.
3. Build directed CSR from layout + orientation recipe.
4. Write passability PPM to `gallery/<map_id>.ppm`.
5. Save orientation JSON recipe under `recipes/`.
6. Append one manifest row with characterization stats.

Re-running `generate` with the same parameters reproduces identical layout, CSR `(V, E)`, recipe JSON, and gallery file hash.

## Query workload

| Parameter | Role |
|-----------|------|
| `pair_seed` | Seeds reachability query pair selection in `ReachabilityBenchmarkJob` |
| `query_count`, `warmup_queries`, `correctness_check_count` | Workload size knobs |

Query generation is independent of maze seeds. `pair_list_hash` in `workload.json` and `results.csv` fingerprints the exact pair list for cross-run comparison.

## Environment notes

- Timers use `std::chrono::steady_clock` (monotonic, not comparable across machines for absolute nanoseconds).
- RSS samples come from `/proc/self/status` (`VmRSS`) during preprocess.
- For published numbers, pin CPU cores (`taskset`), disable turbo if needed, and record `metadata.json` host fields alongside results.
