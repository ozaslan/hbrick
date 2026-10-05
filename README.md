# hbrick

hbrick is a C++20 library for exact directed reachability on grid maps. It builds a compressed sparse row graph from a Moving AI grid, orients the edges with a recorded recipe, and answers reachability queries with search, with flat BRICK, and with H-BRICK.

H-BRICK partitions the map into tiles, replaces each tile by the Boolean reachability among its boundary ports, and composes those summaries up a hierarchy. A query reads the hierarchy and returns whether one passable cell can reach another. Flat BRICK is the same idea on a single level. The library also includes the baselines used in the evaluation: breadth-first search, depth-first search, SCC condensation, GRAIL, O'Reach, 2-hop labeling, and full transitive closure.

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

`tools/run_manuscript_benchmarks.sh` runs the measurements behind the published tables. It imports every recipe, then writes two CSV files under `campaigns/manuscript/`.

`results.csv` holds the single-pair matrix. `HBrickSkipLift` is timed at every base tile in {4, 8, 16, 24, 32, 48, 64, 96} and every group size in {2, 4, 8}. Flat `BrickSearch` is timed at each of those tile sizes. At tile 24 and group 4 the same file also records `CsrBfs`, `SccDagSearch`, `Grail`, `Oreach`, and `TwoHop`. Each of those jobs times 8192 queries after 128 warmup queries, in chunks of 128. An index that would pass 4 GiB is skipped. A preprocess that runs longer than one hour is stopped.

`batch.csv` holds the many-to-many matrix: batch sizes 4, 16, and 64, two warmup batches and eleven timed repetitions, at tile 24 and group 4, on every recipe.

```bash
./tools/run_manuscript_benchmarks.sh
```

The full matrix is thousands of jobs. `--resume` is already on, and extra arguments go to the campaign run, so a machine can take a slice and continue later:

```bash
./tools/run_manuscript_benchmarks.sh --max-jobs 4
HBRICK_SKIP_BATCH=1 ./tools/run_manuscript_benchmarks.sh --max-jobs 4
```

The same single-pair matrix is the campaign preset `manuscript` with config sweep `manuscript`. The short commands below are one recipe at a time.

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
