# Kleene / Warshall variant matrix (campaign)

Closure preprocess in BRICK/H-BRICK uses **K3** (SCC-compressed Kleene) in production, with fallback to vertex-level K2/K1 when the condensation has one vertex per graph vertex (`C = V`).

## Variant IDs

| ID | Implementation | Campaign exposure |
|----|----------------|-------------------|
| K0 | Powers of \(A\) up to \(A^{N-1}\) | `kleene_closure_benchmark` micro-study only |
| K1 | \((I+A)\) squaring, \(\lceil\log_2 n_{\max}\rceil\) cap | `kleene_closure_benchmark` (`k_trunc`) |
| K2 | K1 + SCC-bounded round count | Production fallback inside tile closure |
| K3 | SCC-compressed \((I+A)\) Kleene on \(C\times C\) | Production default (`BrickClosure`, `HBrick`) |
| W0 | Full Warshall on reflexive matrix | `FullClosure` baseline; `--preset kleene-oracle` |
| W1 | Warshall on condensation DAG | `SccDagClosure` baseline; `--preset kleene-oracle` |

## End-to-end campaign presets

```bash
# Closure oracles + BRICK closure on the same maps and query workload
./build/dev/src/bench/hbrick_benchmark_campaign run ./campaigns/kleene \
  --preset kleene-oracle --configs default
```

Results rows record `kleene_rounds_scheduled` and `kleene_rounds_effective` for `BrickClosure` when the baseline exposes round counts.

## Preprocess-only micro-benchmark

The main campaign measures end-to-end query throughput. For Kleene-vs-Warshall timing on synthetic graphs, use the separate binary:

```bash
cmake --build --preset dev --target kleene_closure_benchmark
./build/dev/src/bench/kleene_closure_benchmark

# Or via campaign CLI wrapper (same directory as hbrick_benchmark_campaign):
./build/dev/src/bench/hbrick_benchmark_campaign kleene-micro
```

See `src/bench/kleene_closure_benchmark.cpp` for families, correctness checks against Warshall, and CSV output.

## Analysis notes

- Do not mix K3 preprocess cost with W1 query model on the same leaderboard row without labeling (`map_class`, `method`, `config_id`).
- `correctness_failed=true` when `correctness_mismatches > 0`; treat QPS on those rows as diagnostic only.
