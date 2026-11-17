# HBRICK

Hierarchical Boundary Reachability Index with Compressed Kleene Closure

![HBRICK: the letter H built from bricks, followed by the word BRICK](images/hbrick.png)

HBRICK (Hierarchical Boundary Reachability Index with Compressed Kleene Closure) is a C++20 library for exact directed reachability on grid maps. It builds a compressed sparse row graph from a Moving AI grid, orients the edges with a recorded recipe, and answers reachability queries with search, with flat BRICK, and with the hierarchical index.

The hierarchical index partitions the map into tiles, replaces each tile by the Boolean reachability among its boundary ports, and composes those summaries up a hierarchy. A query reads the hierarchy and returns whether one passable cell can reach another. `Flat BRICK` (`BrickSearch`) is the single-level ablation that keeps the base-tile closures and searches the flat port graph. Canonical `H-BRICK` (`HBrick`) ascends the hierarchy level by level, and `H-BRICK-Skip` (`HBrickSkipLift`) precomputes multi-level boundary-to-boundary skip-lift matrices to jump directly from the base tile to any common ancestor. Base and interface closures use an adaptive bit-parallel backend that selects packed Warshall for dimensions up to 512 (calibratable via `hbrick::calibrateHybridWarshallThreshold`) and M4RM repeated squaring above that threshold. The library also includes the baselines used in the evaluation: breadth-first search, depth-first search, SCC condensation, GRAIL, O'Reach, exact 2-hop labeling, and full transitive closure.

This repository is only that library. It contains the sources, the tests, the command-line benchmarks, the eight evaluation maps, and the 48 orientation recipes. GRAIL and O'Reach are included under `third_party/` with their own license files.

## Using the library

There is no installed package. Add this repository as a subdirectory and link the targets you call. `hbrick_baselines` brings in the graph, tile, and bit libraries. Map and recipe loading is `hbrick_io`.

```cmake
add_subdirectory(hbrick)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE hbrick_baselines hbrick_io)
```

The next two programs share one setup. A recipe names a Moving AI map and the directed orientation. Vertex ids are row-major cell indexes from `MazeLayout::cellIndex`.

Load the Boston street map in regime V01 and ask BFS whether one cell reaches another:

```cpp
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/io/recipe.hpp"

const auto recipe = hbrick::tools::loadRecipe(
    "recipes/M01_V01_ra_target95__street__Boston_0_256.map__random_asymmetric__0000000000000001__feb47d3d.json");
const auto map_path = std::filesystem::path("datasets/movingai")
    / recipe->set_name / "maps" / recipe->map_name;
const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(map_path);
const hbrick::MazeLayout layout = loaded.map.toMazeLayout(recipe->policy);

hbrick::RandomAsymmetricParams orientation;
orientation.seed = recipe->seed;
orientation.p_one_way = recipe->p_one_way;
orientation.p_bidirectional = recipe->p_bidirectional;
orientation.gradient_angle_degrees = recipe->gradient_angle_degrees;
orientation.p_against_gradient = recipe->p_against_gradient;

const hbrick::DirectedGridGraph grid =
    hbrick::DirectedGridGraphBuilder::build(layout, recipe->mode, orientation);
const hbrick::CsrGraph& graph = grid.csrGraph();

const auto source = static_cast<uint32_t>(layout.cellIndex(hbrick::GridCoord{10, 10}));
const auto target = static_cast<uint32_t>(layout.cellIndex(hbrick::GridCoord{40, 25}));
hbrick::GraphSearchScratch scratch(graph.numVertices());
const hbrick::ReachabilityAnswer bfs_answer =
    hbrick::Bfs::reachable(graph, source, target, scratch);
```

Build the `H-BRICK-Skip` (`HBrickSkipLift`) index at the published operating point, tile side `b = 24` and group size `g = 4`, and ask the same question. `preprocess` is the offline build. `query` does not allocate.

```cpp
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/tile/hbrick_config.hpp"

hbrick::HBrickConfig config;
config.base_tile_size = hbrick::TileSize{24, 24};
config.group_size = hbrick::GroupSize{4, 4};
config.max_depth = hbrick::kHBrickFullDepth;

hbrick::HBrickSkipLiftBaseline index;
index.preprocess(grid, layout, config);
const hbrick::ReachabilityAnswer indexed =
    index.query(source, target);
```

`indexed` is `hbrick::ReachabilityAnswer::Reachable` or `Unreachable`. `index.status()` is `BaselineStatus::Completed` when the build finished.

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

- `build/dev/tools/hbrick_manuscript_bench` — reproduces the complete manuscript table and figure datasets across all 48 instances
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

Run these from the repository root after the build above. The reported `H-BRICK-Skip` operating point is base tile `b = 24` and group size `g = 4` (`HBrickSkipLift`), with the default hybrid closure threshold `theta_hyb = 512` (calibratable per host via `hbrick::calibrateHybridWarshallThreshold`). Map M8 needs several gigabytes of RAM for unpruned external baselines.

### Reproducing the manuscript results

`tools/run_manuscript_benchmarks.sh` orchestrates the complete empirical evaluation of the manuscript over the 48 active instances (`M1`–`M8` × `V01`–`V06`) in `recipes/` and `datasets/movingai/`, audits every row for `0` oracle mismatches against `CsrBFS` (and `bfs-per-source` for batches), and summarizes all tables and figure datasets into `campaigns/manuscript/MANUSCRIPT_REPORT.md` and `campaigns/manuscript/figure_data/`.

Every single-pair workload evaluates 8,192 deterministically balanced query pairs (`pair_seed = 0xBEEFCAFE`) after 128 warmup queries, timed in 128-query chunks via `std::chrono::steady_clock`. Index preprocessing enforces a 2.5 GiB retained-index memory cap (further guarded on 4 GiB hosts to keep at least 1.2 GiB OS headroom) and a 300 s timeout for `TwoHop`. All runs pin execution to a single CPU core (`taskset -c 0` when available) and record environment details in `campaigns/manuscript/host_info.txt`.

| Stage (`--stage`) | Runner binary | Output CSVs under `campaigns/manuscript/` | Manuscript targets |
|---|---|---|---|
| `graph_stats` | `hbrick_manuscript_bench` | `graph_and_calibration.csv` | Tables 4 & 5 (map dimensions, `M_geom`, SCC counts, reachability density) |
| `param_sweep` | `hbrick_manuscript_bench` | `param_sweep_results.csv` | Tables 6–9 (`H-BRICK-Skip` `b in {4,8,16,24,32,48,64,96} x g in {2,4,8}` and `Flat BRICK` sweep) |
| `threeway` | `hbrick_manuscript_bench` | `threeway_b24_g4_results.csv`, `threeway_b24_g4_results_raw.csv` | Table 10 (matched 3-pass cyclic ablation: `Flat BRICK`, `H-BRICK`, `H-BRICK-Skip` at `b=24, g=4`) |
| `external` | `hbrick_manuscript_bench` | `lifecycle_results.csv` | Tables 14–16, Section 7.6 (`TwoHop` coverage), Figs. 6a & 7 (`BFS`, `DFS`, `SCC-DAG`, `GRAIL`, `O'Reach`, `TwoHop`, BRICK family) |
| `batch_multisample` | `hbrick_manuscript_bench` | `batch_multisample.csv` | Tables 11–13, Fig. 6b (`k in {4,16,64}`, 10 independent draws, 2 warmup + 11 alternating-order passes vs. `scalar`, `bfs-per-source`, `scc-dag-per-source`, `scc-dag-dedup`, `oreach`) |
| `polarity_and_v06` | `hbrick_manuscript_bench` | `polarity_split.csv`, `v06_sensitivity.csv` | Tables 17 & 18 (`R=1` vs. `R=0` latency split across 48 instances; 160-instance `(theta, p_back)` gradient sweep) |
| `closure_kernels` | `hbrick_manuscript_bench` | `closure_kernel_matrix.csv`, `closure_tile_ablation.csv` | Sections 6.1 & 7.1 (Warshall vs. M4RM across `d=64..2048` and `b=16..48` byte-identical ablation) |
| `batch_equal_pairs` | `hbrick_batch_pilot` | `batch_equal_pairs.csv` | Table 19 (65,536 equal-pair budget across `k in {4,16,32,64}` vs. `O'Reach`) |
| `summary` | `summarize_manuscript_results.py` | `MANUSCRIPT_REPORT.md`, `figure_data/*.csv` | Rebuilds all report tables and plotting CSVs; verifies `0` mismatches |

Quick smoke test (runs a reduced subset of every stage and generates `MANUSCRIPT_REPORT.md`):

```bash
./tools/run_manuscript_benchmarks.sh --quick
```

Full reproduction on a single server (automatically resumes completed rows if interrupted):

```bash
./tools/run_manuscript_benchmarks.sh
```

Run specific stages or shard across multiple machines (for example 4 identical workers):

```bash
# Run selected stages only:
./tools/run_manuscript_benchmarks.sh --stage external,threeway,summary

# On worker node i in {0, 1, 2, 3}:
./tools/run_manuscript_benchmarks.sh --shard-index <i> --shard-count 4 --out-dir campaigns/shard_<i>

# After copying shard CSVs into campaigns/manuscript/ and concatenating shards (keeping 1 header per CSV):
for f in graph_and_calibration.csv param_sweep_results.csv threeway_b24_g4_results.csv \
         threeway_b24_g4_results_raw.csv lifecycle_results.csv batch_multisample.csv \
         polarity_split.csv v06_sensitivity.csv closure_kernel_matrix.csv \
         closure_tile_ablation.csv batch_equal_pairs.csv; do
    awk 'FNR==1 && NR!=1 {next} {print}' campaigns/shard_*/"$f" > campaigns/manuscript/"$f" 2>/dev/null || true
done
./tools/run_manuscript_benchmarks.sh --stage summary
```

#### Verifying on Hetzner Cloud (Hardware & Compiler Parity)

The published manuscript timings were measured on **Hetzner Cloud Regular Performance AMD `CPX22`** instances (`AMD EPYC-Genoa Processor`, Family 25 Model 17, 2 vCPU, 4 GB RAM, 32 MiB L3 cache, Ubuntu 24.04/26.04 LTS, `GCC 15.2` with `-O3 -march=native -flto`, single-core pinned via `taskset -c 0`). Avoid *Cost-Optimized* `CX23` instances, which land on older Xeon Skylake or EPYC-Rome hosts with lower clock and smaller per-core cache. On Ubuntu 24.04/26.04:

```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake libboost-dev python3
cmake --preset dev
cmake --build --preset dev
./tools/run_manuscript_benchmarks.sh
```

The break-even query count reported in `MANUSCRIPT_REPORT.md` (Table 14) is computed from the matched `CsrBFS` and `HBrickSkipLift` rows at `(b = 24, g = 4)` as `Q_BE = T_pre / (T_search - T_query)`. When `HBrickSkipLift` is not faster than `CsrBFS` (such as `M1` under `V02`–`V06`), no finite threshold exists.

### Short single-instance commands

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

The paper is under review. It is not published. Do not cite it as a journal article, and do not invent a volume, issue, pages, or DOI.

Özaslan, T.; Özaslan, E.A. H-BRICK: Hierarchical Boundary Reachability Index with Compressed Kleene Closure for Directed Grid Graphs. Under review at Mathematics (MDPI), 2026.

```bibtex
@unpublished{ozaslan2026hbrick,
  author = {Özaslan, Tolga and Özaslan, Elif A.},
  title  = {H-BRICK: Hierarchical Boundary Reachability Index with Compressed {Kleene} Closure for Directed Grid Graphs},
  year   = {2026},
  note   = {Under review at Mathematics (MDPI). Not yet published. Software: https://github.com/ozaslan/hbrick}
}
```

The maps should also be credited to Sturtevant, N.R. Benchmarks for Grid-Based Pathfinding. IEEE Transactions on Computational Intelligence and AI in Games, 2012.
