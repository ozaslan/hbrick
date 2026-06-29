# Benchmark campaign CLI

Headless benchmark campaigns write reproducible artifacts under a single directory. The implementation reuses `ReachabilityBenchmarkJob`, `hashReachabilityQueryPairs`, `maze_generator`, and `tools::Recipe`.

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
  manifest.csv       # map catalog with characterization (schema v2)
  results.csv        # one row per (map, method)
  summary.md         # human-readable rollup
  gallery/           # passability PPM images
  recipes/           # orientation recipes (JSON)
  logs/              # per-run timestamped log files
```

Schema version: `2` (`kBenchmarkCampaignSchemaVersion`). See `docs/benchmark_campaign_seeding.md` for the seeding contract.

## Commands

```bash
# Create directory layout and headers only
./build/dev/src/bench/hbrick_benchmark_campaign init ./campaigns/run01 --id run01

# Generate procedural mazes (manifest + gallery + recipes, no benchmarks)
./build/dev/src/bench/hbrick_benchmark_campaign generate ./campaigns/run01 \
  --id run01 --count 4 --logical-width 5 --logical-height 5 \
  --carve-seed 1 --opening-seed 2 --extra-openings 3

# Import MovingAI maps (requires extracted datasets on disk)
./build/dev/src/bench/hbrick_benchmark_campaign import-movingai ./campaigns/movingai \
  --datasets-root ./datasets/movingai/extracted \
  --movingai-set dao --movingai-map arena2.map

# Import one smallest map per benchmark set (smoke catalog)
./build/dev/src/bench/hbrick_benchmark_campaign import-movingai ./campaigns/movingai \
  --movingai-smoke

# Run benchmarks from manifest
./build/dev/src/bench/hbrick_benchmark_campaign run ./campaigns/run01 --id run01 \
  --preset all --resume

# Run a subset of methods on one map
./build/dev/src/bench/hbrick_benchmark_campaign run ./campaigns/run01 \
  --method CsrBfs --method BrickSearch --map proc_w5h5_c1_o3_open2_random_asymmetric_i0

# Init + small smoke benchmark
./build/dev/src/bench/hbrick_benchmark_campaign smoke ./campaigns/smoke --id smoke
```

### Method presets

| Preset | Methods |
|--------|---------|
| `smoke` | CsrBfs, BrickSearch, BrickClosure, HBrick |
| `csr` | CsrBfs, CsrDfs, SccDagSearch, SccDagClosure, TwoHop, Grail, FullClosure |
| `brick` | BrickSearch, BrickClosure, HBrick |
| `all` | CSR preset + brick preset (default for `run`) |

Override with `--method NAME` (repeatable) or `--methods CsrBfs,BrickSearch`.

## Library API

- `benchmark_campaign.hpp` — layout, metadata, `runBenchmarkCampaignGridJob`
- `benchmark_campaign_dataset.hpp` — procedural + MovingAI generation
- `benchmark_campaign_run.hpp` — `runBenchmarkCampaignFromManifest`
- `benchmark_campaign_config.hpp` — method presets and name parsing
- `benchmark_campaign_log.hpp` — per-run log files under `logs/`
- `benchmark_campaign_gallery.hpp` — headless passability PPM export

## Tests

```bash
ctest --preset dev -R BenchmarkCampaign
```
