# hbrick

hbrick is a C++20 graph reachability library focused on directed graphs,
grid-embedded mazes, and high-performance query paths.

## Modules

| Module | Purpose |
|--------|---------|
| @ref hbrick_core | Shared types, vertex ids, and query descriptors |
| @ref hbrick_grid | Rectangular passable grids and cardinal directions |
| @ref hbrick_bit | Bit vectors, bit matrices, and boolean closure |
| @ref hbrick_graph | CSR graphs, search, SCC decomposition, and DAG reachability |
| @ref hbrick_baselines | Reference preprocess/query algorithms for correctness and benchmarking |
| @ref hbrick_bench | Lightweight timing helpers |
| @ref hbrick_viz | SVG rendering of grid graphs |
| @ref hbrick_test_support | Maze fixtures and BFS oracle utilities used in tests |

## Typical workflow

1. Build or load a @ref hbrick::PassableGrid or @ref hbrick::CsrGraph.
2. Optionally convert the grid with @ref hbrick::DirectedGridGraphBuilder.
3. Answer reachability using search (@ref hbrick::Bfs, @ref hbrick::Dfs),
   condensation (@ref hbrick::CondensationGraph, @ref hbrick::DagReachability),
   or a baseline for comparison.

## Generating this documentation

From the build directory:

```bash
cmake -S . -B build -DHBRICK_BUILD_DOCS=ON
cmake --build build --target hbrick_docs
```

HTML output is written to `docs/html/index.html` at the repository root
(configurable via `HBRICK_DOCS_OUTPUT_DIR`, default: source-tree `docs/`).
