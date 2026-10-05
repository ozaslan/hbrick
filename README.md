# HBRICK

Hierarchical Boundary Reachability Index with Compressed Kleene Closure

![HBRICK: the letter H built from bricks, followed by the word BRICK](images/hbrick.png)

HBRICK (Hierarchical Boundary Reachability Index with Compressed Kleene Closure) is a C++20 library for exact directed reachability on grid maps. It builds a compressed sparse row graph from a Moving AI grid, orients the edges with a recorded recipe, and answers reachability queries with search, with flat BRICK, and with the hierarchical index.

The hierarchical index partitions the map into tiles, replaces each tile by the Boolean reachability among its boundary ports, and composes those summaries up a hierarchy. A query reads the hierarchy and returns whether one passable cell can reach another. Flat BRICK is the same idea on a single level: it keeps the tile closures and searches the flat port graph. The library also includes the baselines used in the evaluation: breadth-first search, depth-first search, SCC condensation, GRAIL, O'Reach, 2-hop labeling, and full transitive closure.

This repository is only that library. It contains the sources, the tests, the command-line benchmarks, the eight evaluation maps, and the 48 orientation recipes. GRAIL and O'Reach are included under `third_party/` with their own license files.

## Maps and recipes

The evaluation uses eight Moving AI grids and six directed orientations on each grid, 48 instances in total. The map files are already unpacked:

```
datasets/movingai/<set>/maps/<file>.map
```

| Paper id | Recipe prefix | Set | Map file | What the grid is |
|----------|---------------|-----|----------|------------------|
| M1 | M03 | maze | maze512-1-0.map | 512 by 512 maze, corridors one cell wide |
| M2 | M04 | maze | maze512-32-0.map | 512 by 512 maze, corridors 32 cells wide |
| M3 | M05 | room | 8room_000.map | 512 by 512 building with eight rooms |
| M4 | M06 | random | random512-20-0.map | 512 by 512 field, about 20 percent obstacles |
| M5 | M02 | dao | arena2.map | Dragon Age arena, 281 by 209 |
| M6 | M01 | street | Boston_0_256.map | Boston streets, 256 by 256 |
| M7 | M07 | street | Boston_0_512.map | Boston streets, 512 by 512 |
| M8 | M09 | street | Boston_0_1024.map | Boston streets, 1024 by 1024 |

The map files, and the citation for the Moving AI collection, are described in [`datasets/movingai/README.md`](datasets/movingai/README.md). A recipe turns one of those occupancy grids into a directed graph. The fields, the six regimes, and the calibrated probabilities are in [`recipes/README.md`](recipes/README.md).

## Requirements

- A C++20 compiler (GCC 11 or newer, or Clang 14 or newer)
- CMake 3.20 or newer
- Boost headers (`libboost-dev` on Debian and Ubuntu)

Google Test is downloaded automatically the first time CMake configures the tests. Optional API documentation needs Doxygen and Graphviz.

## Build

From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev
```

The `dev` preset is a Release build with warnings treated as errors. Binaries land in `build/dev/`. The benchmark programs are:

- `build/dev/tools/hbrick_quick_bench` — one recipe, one method
- `build/dev/tools/hbrick_batch_pilot` — one recipe, a batch of sources and targets
- `build/dev/src/bench/hbrick_benchmark_campaign` — a campaign over many recipes

To generate the HTML API reference as well:

```bash
cmake --preset dev -DHBRICK_BUILD_DOCS=ON
cmake --build --preset dev --target hbrick_docs
```

Open `docs/html/index.html`. The pages are generated from the public headers.

## Tests

The default build compiles every test. Each source under `tests/unit/` and `tests/integration/` becomes a program in `build/dev/tests/`. CTest then runs the Google Test cases inside those programs. A case that runs longer than its limit fails. Most cases are limited to 20 seconds. Cases marked slow have a longer limit, up to 15 minutes for the large-map query bench.

Run the whole suite from the repository root, after the build above:

```bash
ctest --preset dev --output-on-failure
```

That includes the unit checks and the integration checks that load the shipped maps and recipes. CTest prints a case only when it fails. The same run without the cases labeled slow:

```bash
ctest --preset dev-fast --output-on-failure
```

List every registered case without running it:

```bash
ctest --test-dir build/dev -N
```

A case name has the form `Suite.Name`, for example `MovingAiLoader.ParsesHeaderAndCells`. Run one case, or every case whose name matches a pattern:

```bash
ctest --test-dir build/dev --output-on-failure -R 'MovingAiLoader'
```

Run one test program directly. This runs every case linked into that program, and it prints each case as it goes:

```bash
./build/dev/tests/test_core_types
./build/dev/tests/test_movingai_loader --gtest_filter='MovingAiLoader.ParsesHeaderAndCells'
```

The map path is compiled into the test programs, so these commands work from the repository root. You do not need to copy the grids next to the binary.

## Benchmarks

Run these from the repository root after the build above. The reported H-BRICK operating point is base tile `b = 24` and group size `g = 4`, using the skip-lift query (`HBrickSkipLift`). The commands below use a short query count so a single instance finishes on a workstation. Map M8 needs several gigabytes of RAM for some baselines.

### Manuscript matrix

`tools/run_manuscript_benchmarks.sh` is the setting that covers the published evaluation. It reads every JSON file in `recipes/` and the map named by that file under `datasets/movingai/`. It writes two CSV files under `campaigns/manuscript/`. The campaign preset and the config sweep are both named `manuscript`. The older preset named `paper` is a different, earlier protocol (tile 16, group 2, method `HBrick`) and is not this matrix.

The script first imports the 48 recipes into a campaign manifest, then runs the single-pair stage, then the batch stage. A finished single-pair row is not repeated: the campaign is started with `--resume`. Extra arguments are forwarded only to that campaign run.

```bash
./tools/run_manuscript_benchmarks.sh
./tools/run_manuscript_benchmarks.sh --max-jobs 4
HBRICK_SKIP_BATCH=1 ./tools/run_manuscript_benchmarks.sh --max-jobs 4
```

`--max-jobs` limits how many recipes the single-pair stage visits in this invocation. The batch stage still walks every recipe unless `HBRICK_SKIP_BATCH=1`.

#### Single-pair file: `results.csv`

Every job uses the same clock protocol. There are 128 untimed warmup queries, then 8192 timed queries, measured with `std::chrono::steady_clock` in chunks of 128. After the timed queries, 256 pairs are checked against the search answer and the mismatch count is stored. Each method runs in its own process so the recorded memory is that method's. An index whose estimated size exceeds 4 GiB is skipped and the row says so. A preprocess that is still running after one hour is stopped.

On each of the 48 recipes the runner builds these configurations.

| What is timed | Tile side b | Group size g | Rows per recipe |
|---------------|-------------|--------------|-----------------|
| `HBrickSkipLift` | 4, 8, 16, 24, 32, 48, 64, 96 | 2, 4, and 8 at every tile | 24 |
| `BrickSearch` (flat BRICK: the same base tiles, then BFS on the flat port graph) | those eight tile sides | not used; the flat encoding stores group 0 | 8 |
| `CsrBfs`, `SccDagSearch`, `Grail`, `Oreach`, `TwoHop` | 24 only | 4 only | 5 |

`HBrickSkipLift` at tile 24 and group 4 is the operating point used everywhere else in the paper. It is one of the 24 hierarchy rows, not a sixth extra copy. The five external methods are attached to that same configuration, so the file can be joined on recipe and on `(b, g) = (24, 4)`. `SccDagSearch` is search on the strongly connected component condensation. `TwoHop` is the exact 2-hop labeler; on the large maps it is the method most likely to be skipped by the 4 GiB cap, which is the outcome reported for most of the 48 instances.

That is 24 + 8 + 5 = 37 measured rows per recipe, and 1776 rows for all 48 recipes, plus any skipped rows that still occupy a line. The hierarchy parameter tables are the 24 `HBrickSkipLift` rows. The flat-versus-hierarchy comparison is `BrickSearch` against `HBrickSkipLift` at the same tile, read at group 4 for the operating point. The external-baseline tables are the five methods plus `HBrickSkipLift` at tile 24 and group 4. The break-even query count is not a separate job. It is `preprocess_time / (bfs_query_time - hbrick_query_time)` computed from the `CsrBfs` and `HBrickSkipLift` rows of that operating point. When the H-BRICK query is not faster than BFS, that ratio is not a finite threshold.

#### Batch file: `batch.csv`

After the single-pair stage, `hbrick_batch_pilot` runs once per recipe at tile 24 and group 4. For each batch size `k` in {4, 16, 64} it draws `k` distinct sources and `k` distinct targets, warms up with two batch calls, then times eleven repetitions. The CSV has one row per method at that `k`. The methods are:

| CSV `method` | What it does |
|--------------|----------------|
| `batch-h-brick` | One batched `HBrickSkipLift` query over the `k` by `k` pairs, sharing source lifts, target lifts, and ancestor frontiers |
| `scalar-h-brick` | The same pairs as independent `HBrickSkipLift` queries, which is the denominator of the batch speedup |
| `oreach` | O'Reach on each pair, with no cross-pair sharing |
| `bfs-per-source` | One BFS per distinct source, answering every target of that source |
| `scc-dag-search` | Search on the condensation DAG |
| `scc-dag-closure` | Boolean closure on the condensation DAG; its preprocess is abandoned after 60 seconds |

The index build for skip-lift H-BRICK, O'Reach, and the SCC-DAG search is recorded on those rows as well. This file is the batch table and the batch-versus-O'Reach comparison. It does not repeat the tile and group sweep.

The short commands below time one recipe with a smaller query count. They are not the manuscript matrix.

One method on paper map M6, regime V01 (Boston, 256 by 256, dense orientation):

```bash
./build/dev/tools/hbrick_quick_bench \
  --recipe recipes/M01_V01_ra_target95__street__Boston_0_256.map__random_asymmetric__0000000000000001__feb47d3d.json \
  --method HBrickSkipLift \
  --b 24 --g 4 \
  --query-count 1000 --warmup-queries 100 \
  --datasets-root datasets/movingai
```

The same instance with breadth-first search, then with O'Reach:

```bash
./build/dev/tools/hbrick_quick_bench \
  --recipe recipes/M01_V01_ra_target95__street__Boston_0_256.map__random_asymmetric__0000000000000001__feb47d3d.json \
  --method CsrBfs \
  --query-count 1000 --warmup-queries 100 \
  --datasets-root datasets/movingai

./build/dev/tools/hbrick_quick_bench \
  --recipe recipes/M01_V01_ra_target95__street__Boston_0_256.map__random_asymmetric__0000000000000001__feb47d3d.json \
  --method Oreach \
  --query-count 1000 --warmup-queries 100 \
  --datasets-root datasets/movingai
```

Other method names accepted by `--method` include `Grail`, `SccDagSearch`, `SccDagClosure`, `TwoHop`, `BrickSearch`, `BrickClosure`, `HBrick`, and `HBrickFusedLift`.

A batch workload on paper map M5 (the arena), regimes left at the default batch sizes, timing H-BRICK and O'Reach:

```bash
./build/dev/tools/hbrick_batch_pilot \
  --recipe recipes/M02_V06_gf_ang045_back05__dao__arena2.map__gradient_flow__0000000000000001__3070eebf.json \
  --datasets-root datasets/movingai \
  --b 24 --g 4 \
  --k 4,16 \
  --core-only
```

A campaign is the way to walk every shipped recipe. This example imports the 48 recipes, then runs skip-lift H-BRICK and BFS on the first job only. Repeat without `--max-jobs` to continue, and add `--resume` to skip rows already written to `campaigns/paper/results.csv`.

```bash
./build/dev/src/bench/hbrick_benchmark_campaign import-recipes ./campaigns/paper \
  --id paper \
  --recipes-dir recipes \
  --datasets-root datasets/movingai

./build/dev/src/bench/hbrick_benchmark_campaign run ./campaigns/paper \
  --id paper \
  --method HBrickSkipLift --method CsrBfs \
  --configs default \
  --brick-tile 24 --hbrick-group 4 \
  --query-count 1000 --warmup-queries 100 \
  --max-jobs 1
```

The importer names each row `mv3_` plus the recipe prefix, so Boston V01 is `--map mv3_M01_V01`. A tiny grid that does not touch the Moving AI maps:

```bash
./build/dev/src/bench/hbrick_benchmark_campaign smoke ./campaigns/smoke --id smoke
```

`hbrick_benchmark_campaign` with no arguments prints the rest of the campaign syntax.

## Layout

```
include/hbrick/     Public headers
src/                Library and hbrick_benchmark_campaign
tests/              Unit and integration tests
tools/              hbrick_quick_bench and hbrick_batch_pilot
third_party/        Vendored GRAIL and O'Reach; see third_party/README.md
datasets/movingai/  The eight evaluation maps
recipes/            The 48 orientation recipes
docs/               Optional HTML API reference (Doxygen)
```

## Citing this work

If you use this software or these instances in academic work, please cite the manuscript:

Özaslan, T.; Özaslan, E.A. H-BRICK: Hierarchical Boundary Reachability Index with Compressed Kleene Closure for Directed Grid Graphs. Manuscript submitted to Mathematics, MDPI, 2026.

```bibtex
@article{ozaslan2026hbrick,
  author  = {Özaslan, Tolga and Özaslan, Elif A.},
  title   = {H-BRICK: Hierarchical Boundary Reachability Index with Compressed {Kleene} Closure for Directed Grid Graphs},
  journal = {Mathematics},
  year    = {2026},
  note    = {Manuscript submitted to Mathematics (MDPI). Software: https://github.com/ozaslan/hbrick}
}
```

The volume, issue, and DOI are not assigned yet. Replace the note with the published citation when the article appears. The maps should also be credited to Sturtevant, N.R. Benchmarks for Grid-Based Pathfinding. IEEE Transactions on Computational Intelligence and AI in Games, 2012.
