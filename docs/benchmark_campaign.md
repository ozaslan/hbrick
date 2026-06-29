# Benchmark campaign CLI

Headless benchmark campaigns write reproducible artifacts under a single directory. The implementation reuses `ReachabilityBenchmarkJob`, `hashReachabilityQueryPairs`, and `process_memory` RSS sampling.

## Build

```bash
cmake --preset dev
cmake --build --preset dev --target hbrick_benchmark_campaign
```

## Campaign layout

```
<campaign_dir>/
  metadata.json      # git commit, compiler, CPU, schema version
  workload.json      # pair_seed, query_count, pair_list_hash
  manifest.csv       # map catalog (one row per map)
  results.csv        # one row per (map, method)
  summary.md         # human-readable rollup
  gallery/           # map images (future)
  recipes/           # orientation recipes (future)
  logs/              # run logs
```

Schema version: `1` (`kBenchmarkCampaignSchemaVersion`).

## Commands

```bash
# Create directory layout and headers only
./build/dev/src/bench/hbrick_benchmark_campaign init ./campaigns/run01 --id run01

# Init + small smoke benchmark (8×8 grid, CsrBfs / BRICK / H-BRICK)
./build/dev/src/bench/hbrick_benchmark_campaign smoke ./campaigns/smoke --id smoke
```

## Library API

See `include/hbrick/bench/benchmark_campaign.hpp`:

- `initializeBenchmarkCampaignDirectory` — create layout
- `runBenchmarkCampaignGridJob` — run `ReachabilityBenchmarkJob` and append CSV/MD
- `captureBenchmarkCampaignMetadata` — host/build metadata
- `workloadFromBenchmarkReport` — links results to `pair_list_hash` from the job

## Tests

```bash
ctest --preset dev -R BenchmarkCampaign
```
