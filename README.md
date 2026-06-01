# hbrick

**hbrick** is a C++20 library for **directed graph reachability** on grid-embedded graphs and general CSR (compressed sparse row) graphs. It provides fast search primitives, bit-parallel closure machinery, SCC condensation, reference baselines, and a correctness harness built around maze fixtures.

The project is the implementation foundation for **H-BRICK** (Hierarchical BRICK), a planned reachability index for large grid graphs. The tile index and query propagation layers are **not implemented yet**; this repository currently delivers the infrastructure those layers will build on.

---

## What is in this repo today

| Area | Status |
|------|--------|
| Core types, grids, CSR graphs | Implemented |
| BFS / DFS search with reusable scratch buffers | Implemented |
| Bit vectors, bit matrices, boolean transitive closure | Implemented |
| SCC decomposition and condensation DAG | Implemented |
| Reference baselines (search + closure) | Implemented |
| Maze-based correctness harness (BFS oracle) | Implemented |
| SVG grid visualization | Implemented |
| Lightweight benchmarking helpers | Implemented |
| Doxygen API documentation | Implemented |
| H-BRICK tile index / hierarchical queries | **Not started** |

---

## Modules

The library is split into target-based CMake modules:

| CMake target | Namespace | Purpose |
|--------------|-----------|---------|
| `hbrick_core` | `hbrick::` | Shared types, vertex IDs, reachability answers, status reporting |
| `hbrick_grid` | `hbrick::` | Rectangular passable grids and cardinal directions |
| `hbrick_graph` | `hbrick::` | CSR storage, BFS/DFS, SCC decomposition, condensation DAG, DAG reachability |
| `hbrick_bit` | `hbrick::` | Bit vectors, bit matrices, boolean closure (pure bit logic) |
| `hbrick_baselines` | `hbrick::` | Reference preprocess/query algorithms for correctness and benchmarking |
| `hbrick_bench` | `hbrick::` | Simple timing utilities |
| `hbrick_viz` | `hbrick::` | SVG rendering of grid graphs |

Typical workflow:

1. Build or load a `PassableGrid` or `CsrGraph`.
2. Optionally convert a grid with `DirectedGridGraphBuilder` (acyclic east/south, bidirectional, or seeded random-asymmetric edges).
3. Answer reachability via search (`Bfs`, `Dfs`), condensation (`SccDecomposition`, `CondensationGraph`, `DagReachability`), or a baseline for comparison.

---

## Requirements

- **C++20** compiler (GCC 11+, Clang 14+, or equivalent)
- **CMake** 3.20 or newer
- **Google Test** (fetched automatically via CMake if not installed system-wide)
- **Doxygen** + **Graphviz** (optional, only for API docs)

---

## Build

The project uses [CMake presets](CMakePresets.json). The recommended development preset enables warnings-as-errors and Release optimizations.

```bash
cmake --preset dev
cmake --build --preset dev
```

Useful CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `HBRICK_WARNINGS_AS_ERRORS` | `ON` (via preset) | Treat compiler warnings as errors |
| `HBRICK_BUILD_DOCS` | `OFF` | Add the `hbrick_docs` Doxygen target |
| `HBRICK_ENABLE_LTO` | `ON` | Link-time optimization for non-Debug builds |

Example with documentation enabled:

```bash
cmake --preset dev -DHBRICK_BUILD_DOCS=ON
cmake --build --preset dev
cmake --build --preset dev --target hbrick_docs
```

Generated HTML is written to [`docs/html/index.html`](docs/html/index.html) (gitignored; regenerate locally or in CI).

---

## Test

All tests are registered with CTest and use Google Test.

```bash
ctest --preset dev --output-on-failure
```

There are **19 test executables**, including:

- **Unit tests** for grids, CSR graphs, bit primitives, SCC/condensation, baselines, and SVG output
- **Integration tests** including a multi-baseline correctness harness
- **`test_maze_reachability`** — exhaustive all-pairs checks on non-trivial perfect and cyclic mazes, comparing every baseline against BFS
- **`test_dag_reachability`** — direct coverage of DAG reachability on known graphs and maze-derived condensation DAGs
- **`test_hot_path_allocations`** — verifies that query/traversal paths do not resize scratch buffers

CI (GitHub Actions) runs configure, build, docs, and the full test suite on every push and pull request.

---

## Repository layout

```
hbrick/
├── include/hbrick/     Public headers (core, grid, graph, bit, baselines, bench, viz)
├── src/                Library implementations
├── tests/
│   ├── unit/           Focused unit tests
│   ├── integration/    Cross-module correctness harnesses
│   └── support/        Maze generator and BFS oracle helpers
├── docs/               Doxygen sources (mainpage, groups); html/ is generated output
├── .github/workflows/  CI pipeline
├── CMakeLists.txt
├── CMakePresets.json
└── codex_implementation_spec.md   Full staged implementation specification
```

---

## Design principles

Hot traversal and query paths are optimized for predictable performance:

- **No heap allocations** inside traversal, search, or query functions
- **No associative containers** on hot paths — contiguous arrays and bit-parallel words instead
- **No virtual dispatch** — flat, statically dispatched code
- **Scratch reuse** via `GraphSearchScratch` with visited-mark stamping and overflow handling

Closure-based baselines estimate memory **before** allocation and return `SkippedByPolicy` when a configured limit would be exceeded.

---

## Baselines

Reference algorithms live under `hbrick_baselines` and share a common preprocess / query interface:

| Baseline | Strategy |
|----------|----------|
| `CsrBfsBaseline` | On-demand BFS per query |
| `CsrDfsBaseline` | On-demand DFS per query |
| `SccDagSearchBaseline` | SCC decomposition + search on condensation DAG |
| `SccDagClosureBaseline` | SCC decomposition + boolean closure on DAG components |
| `FullClosureBaseline` | Full boolean transitive closure of the graph |

These are used for correctness checking and future benchmarking, not as the final H-BRICK index.

---

## Documentation

- **API reference**: build with `-DHBRICK_BUILD_DOCS=ON`, then open `docs/html/index.html`
- **Implementation spec**: [`codex_implementation_spec.md`](codex_implementation_spec.md) describes staged requirements, acceptance criteria, and the roadmap through H-BRICK

---

## Roadmap

Stages 0–10 (infrastructure, baselines, visualization, correctness harness) are largely complete.

**Stage 11+** (H-BRICK tiles, parent composition, query propagation) is blocked until port rules, pseudocode, and hand-verified composition examples are finalized. See the readiness statement at the end of `codex_implementation_spec.md`.

---

## Version

Current library version: **0.1.0** (see `hbrick::kLibraryVersion`).
