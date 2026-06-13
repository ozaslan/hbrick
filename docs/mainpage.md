# hbrick

hbrick is a C++20 graph reachability library focused on directed graphs,
grid-embedded mazes, and high-performance query paths.

## Modules

| Module | Purpose |
|--------|---------|
| @ref hbrick_core | Shared types, vertex ids, and query descriptors |
| @ref hbrick_io | MovingAI `.map` loading and passability policies |
| @ref hbrick_grid | Maze layouts and cardinal directions |
| @ref hbrick_bit | Bit vectors, bit matrices, and boolean closure |
| @ref hbrick_graph | CSR graphs, search, SCC decomposition, DAG reachability, and reachability density |
| @ref hbrick_baselines | Reference preprocess/query algorithms for correctness and benchmarking |
| @ref hbrick_bench | Lightweight timing helpers |
| @ref hbrick_viz | SVG rendering of grid graphs |
| @ref hbrick_test_support | Maze fixtures, MovingAI catalog, recipe graph builder, and reachability oracle utilities used in tests |

## Typical workflow

1. Build or load a @ref hbrick::MazeLayout or @ref hbrick::CsrGraph.
2. Optionally convert the grid with @ref hbrick::DirectedGridGraphBuilder.
3. Answer reachability using search (@ref hbrick::Bfs, @ref hbrick::Dfs,
   condensation (@ref hbrick::CondensationGraph, @ref hbrick::DagReachability),
   a baseline for comparison, or @ref hbrick::ReachabilityDensityEstimator for
   sampled connectivity density.

## Further reading

- [Atlas](atlas.md) — quick reference for every type, data structure, and algorithm
- [Representations guide](representations.md) — how mazes, grids, and graphs relate and why conversion happens
- [Traversal storage](traversal_storage.md) — why CSR is used for BFS, DFS, and SCC; sparse formats vs hash tables
- [Closure storage](closure_storage.md) — why reachability oracles use dense BitMatrix and Warshall
- [GraphSearchScratch design notes](graph_search_scratch.md) — reusable traversal workspace details
- [Reachability density](reachability_density.md) — sampled reachable-pair fraction, distinct sources, parallel BFS
- [Dataset browser](dataset_browser.md) — interactive GUI for MovingAI maps, directed graphs, and orientation recipes

## Generating this documentation

From the build directory:

```bash
cmake -S . -B build -DHBRICK_BUILD_DOCS=ON
cmake --build build --target hbrick_docs
```

HTML output is written to `docs/html/index.html` at the repository root
(configurable via `HBRICK_DOCS_OUTPUT_DIR`, default: source-tree `docs/`).
