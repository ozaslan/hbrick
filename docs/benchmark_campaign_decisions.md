# Benchmark campaign — design decisions

Recorded answers to open questions in `docs/benchmark_campaign_todo.md` §13.

## Results storage

**Decision:** One append-only `results.csv` per campaign directory.

- Resume keys on `(map_id, method, config_id)` with status `Completed`.
- Large campaigns stay simple to grep, plot, and version-control as a single artifact.
- Per-(map, method) files remain a future option if row count becomes unwieldy.

## Memory budget

**Decision:** Per-case `max_memory_bytes` in `ReachabilityBenchmarkConfig` (default 512 MiB in presets).

- No global campaign memory cap yet; operators set `--configs` and tile sweeps to bound worst case.
- Skipped rows record `SkippedByPolicy` / `OutOfMemory` with `policy_skip_detail`.

## Reachability density in manifest

**Decision:** Not computed by default.

- Manifest v2 already carries SCC counts, component sizes, and edge counts for bucketing.
- Optional `ReachabilityDensityEstimator` samples may be added later for maps below a vertex threshold.

## Gallery content

**Decision:** Passability PPM only in v1 campaigns.

- Orientation overlay, SCC coloring, and BRICK tile boundaries are useful for debugging but not required for benchmark reproducibility.
- `gallery_image_hash` in the manifest pins the current renderer output.

## Leaderboards

**Decision:** Publish separate views from the same `results.csv`:

| View | Primary columns |
|------|-----------------|
| Query throughput | `queries_per_second`, `correctness_failed=false` |
| Index size | `estimated_index_bytes`, `peak_preprocess_rss_bytes` |
| End-to-end | `total_benchmark_ns`, `total_speedup_vs_bfs` |

`summary.md` is regenerated from the full results file and highlights best completed QPS per `map_class`.
