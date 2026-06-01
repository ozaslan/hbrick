# H-BRICK C++ Library

## Final Codex-Safety Addendum Before Implementation

## 0. Purpose of This Addendum

This addendum amends the Codex-ready implementation specification before it is used as the primary instruction document for Codex, Cursor, Windsurf, or any similar AI coding assistant.

It resolves the remaining implementation ambiguities identified during review:

1. Testing framework was not explicitly named.
2. `BooleanClosure::reflexiveAdjacencyFromGraph` created a layering and memory-safety problem.
3. `hbrick_bit` incorrectly depended on `hbrick_graph`.
4. Random graph generation did not specify a reproducible RNG procedure.
5. `TimestampMarker` was listed but not defined.
6. Hot-path graph APIs mixed `VertexId` and raw `uint32_t`.
7. `SvgCanvas::text` did not require XML escaping.
8. `visited_mark` overflow was not handled.
9. Single-threaded execution was implied but not explicit.
10. Codex prompting workflow needed a practical rule for how to feed the document.

These corrections are mandatory before Stage 0 begins.

---

# 1. Testing Framework Decision

The project shall use **Google Test** for all unit, integration, and regression tests.

Codex must generate tests using:

```cpp
#include <gtest/gtest.h>
```

and the standard form:

```cpp
TEST(SuiteName, TestName) {
    // expectations
}
```

Do not use Catch2, doctest, custom assertion macros, or ad hoc executable-based tests unless explicitly requested.

## Stage 0 CMake Requirement

Stage 0 must set up Google Test in one of two acceptable ways:

Preferred:

```cmake
find_package(GTest CONFIG REQUIRED)
```

Fallback only if no system package is available:

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)
```

All tests shall be registered with CTest.

Example:

```cmake
add_executable(test_csr_graph tests/unit/test_csr_graph.cpp)
target_link_libraries(test_csr_graph PRIVATE hbrick_graph GTest::gtest_main)
add_test(NAME test_csr_graph COMMAND test_csr_graph)
```

---

# 2. Corrected Target Dependency Graph

The previous staged specification incorrectly allowed `hbrick_bit` to depend on `hbrick_graph`. This must be fixed.

## Correct Initial Target Dependencies

```text
hbrick_core:
    no internal dependencies

hbrick_io:
    hbrick_core
    hbrick_grid

hbrick_grid:
    hbrick_core

hbrick_graph:
    hbrick_core
    hbrick_grid

hbrick_bit:
    hbrick_core

hbrick_baselines:
    hbrick_core
    hbrick_grid
    hbrick_graph
    hbrick_bit

hbrick_viz:
    hbrick_core
    hbrick_grid
    hbrick_graph

hbrick_bench:
    hbrick_core
```

## Rule

`BitVector`, `BitMatrix`, and `BooleanClosure` are pure Boolean-algebraic utilities. They must not include graph headers and must not know about `CsrGraph`.

Therefore:

```text
include/hbrick/bit/boolean_closure.hpp
```

must not include:

```cpp
#include "hbrick/graph/csr_graph.hpp"
```

---

# 3. Corrected BooleanClosure Design

## 3.1 Remove This Function

The following function must not exist in `BooleanClosure`:

```cpp
static BitMatrix reflexiveAdjacencyFromGraph(const CsrGraph& graph);
```

Reason:

1. It creates a dependency from `hbrick_bit` to `hbrick_graph`.
2. It invites accidental full-graph closure allocation on large maps.
3. It hides the memory decision inside a low-level bit utility.

## 3.2 Correct `BooleanClosure` Interface

Use this interface instead:

```cpp
class BooleanClosure {
public:
    static void transitiveClosureWarshallInPlace(BitMatrix& relation);

    static BitMatrix transitiveClosureWarshall(BitMatrix relation);
};
```

`BooleanClosure` only performs closure on an already allocated relation.

It does not allocate graph-derived matrices by itself.

---

# 4. New Graph-to-BitMatrix Adapter

Graph-to-relation construction belongs outside `hbrick_bit`.

Create:

```text
include/hbrick/baselines/closure_matrix_builder.hpp
src/baselines/closure_matrix_builder.cpp
```

## Interface

```cpp
class ClosureMatrixBuilder {
public:
    static uint64_t estimateReflexiveAdjacencyBytes(uint32_t num_vertices) noexcept;

    static bool canAllocateReflexiveAdjacency(
        uint32_t num_vertices,
        uint64_t max_memory_bytes
    ) noexcept;

    static BitMatrix buildReflexiveAdjacencyOrThrow(
        const CsrGraph& graph,
        uint64_t max_memory_bytes
    );
};
```

## Behavior

`estimateReflexiveAdjacencyBytes(n)` computes:

```text
n * ceil(n / 64) * sizeof(uint64_t)
```

`canAllocateReflexiveAdjacency(n, max_memory_bytes)` returns false if the estimated bytes exceed the configured limit.

`buildReflexiveAdjacencyOrThrow(graph, max_memory_bytes)` shall:

1. Estimate memory.
2. Throw a preprocessing-level exception if the matrix exceeds the memory limit.
3. Allocate the `BitMatrix`.
4. Set identity entries.
5. Set graph edge entries.
6. Return the matrix.

This function is allowed to throw because it is preprocessing code, not a query hot path.

## FullClosureBaseline Requirement

`FullClosureBaseline::preprocess()` must call `ClosureMatrixBuilder::canAllocateReflexiveAdjacency()` before allocation.

If the estimate exceeds the limit:

```cpp
status_ = BaselineStatus::SkippedByPolicy;
```

No allocation shall be attempted.

## SCC-DAG Closure Requirement

`SccDagClosureBaseline` must apply the same guard to the condensation DAG.

The fact that a graph is a DAG does not make its closure matrix small.

---

# 5. Deterministic RNG Requirement

All random directed graph conversion must use:

```cpp
std::mt19937_64
```

seeded exactly once with:

```cpp
RandomAsymmetricParams::seed
```

## Edge-Pair Enumeration Order

Adjacent passable cell pairs must be enumerated in deterministic row-major order:

```text
for y = 0 ... height-1:
    for x = 0 ... width-1:
        consider only East and South neighbors for 4-neighbor pair enumeration
```

This prevents duplicate pair handling and makes the random sequence stable.

## No `std::default_random_engine`

Forbidden:

```cpp
std::default_random_engine
std::random_device
```

inside graph generation.

## Avoid `std::uniform_real_distribution` for Cross-Platform Reproducibility

To avoid implementation-dependent floating distribution behavior, random decisions shall use raw `uint64_t` samples from `std::mt19937_64`.

Recommended procedure:

```cpp
uint64_t r = rng();

uint64_t threshold_bi =
    static_cast<uint64_t>(params.p_bidirectional * max_uint64_as_long_double);

uint64_t threshold_one =
    static_cast<uint64_t>((params.p_bidirectional + params.p_one_way)
                          * max_uint64_as_long_double);
```

Then compare `r` against integer thresholds.

If `r < threshold_bi`, add both directions.

Else if `r < threshold_one`, use one-way direction. The one-way orientation can be selected by consuming one additional bit or one additional RNG draw.

Else, add no edge.

The exact procedure must be documented in a test.

## Required Tests

The directed graph builder tests must verify:

```text
same seed produces identical graph
different seed usually produces different graph
row-major pair order is deterministic
bidirectional mode does not consume RNG
deterministic acyclic east-south mode does not consume RNG
```

---

# 6. Remove or Replace TimestampMarker

The initial staged implementation must not include:

```text
include/hbrick/bit/timestamp_marker.hpp
src/bit/timestamp_marker.cpp
```

Reason:

`TimestampMarker` is not a bit-matrix primitive. It belongs to graph search scratch state, not the bit layer.

## Replacement: GraphSearchScratch

Create:

```text
include/hbrick/graph/graph_search_scratch.hpp
src/graph/graph_search_scratch.cpp
```

## Interface

```cpp
class GraphSearchScratch {
public:
    GraphSearchScratch() = default;
    explicit GraphSearchScratch(uint32_t num_vertices);

    void resetForGraph(uint32_t num_vertices);

    uint32_t nextMark();

    std::vector<uint32_t>& queue() noexcept;
    std::vector<uint32_t>& stack() noexcept;
    std::vector<uint32_t>& visitedMark() noexcept;

    const std::vector<uint32_t>& visitedMark() const noexcept;

    size_t memoryBytes() const noexcept;

private:
    std::vector<uint32_t> queue_;
    std::vector<uint32_t> stack_;
    std::vector<uint32_t> visited_mark_;

    uint32_t current_mark_ = 1;
};
```

## Marking Semantics

`visited_mark_` is preallocated to `num_vertices`.

For each query:

```cpp
uint32_t mark = scratch.nextMark();
```

A vertex `v` is unvisited if:

```cpp
visited_mark_[v] != mark
```

A vertex is marked visited by:

```cpp
visited_mark_[v] = mark;
```

This avoids clearing the entire visited array between queries.

## Overflow Handling

If `current_mark_` reaches:

```cpp
std::numeric_limits<uint32_t>::max()
```

then `nextMark()` must:

1. Fill `visited_mark_` with zeros.
2. Reset `current_mark_` to `1`.
3. Return `1`.

This prevents silent wraparound bugs.

## No Allocation in BFS/DFS

`Bfs::reachable()` and `Dfs::reachable()` must not resize scratch buffers. Scratch must already be sized correctly.

---

# 7. Standardize Hot-Path Graph IDs

The initial implementation shall use raw `uint32_t` vertex IDs in all hot graph traversal APIs.

Public semantic APIs may use `VertexId`, but CSR traversal APIs must use `uint32_t`.

## Rule

`CsrGraph::outNeighbors()` returns:

```cpp
std::span<const uint32_t>
```

`DirectedGridGraph::outNeighbors()` also returns:

```cpp
std::span<const uint32_t>
```

`Bfs::reachable()` uses:

```cpp
uint32_t source;
uint32_t target;
```

`Dfs::reachable()` uses:

```cpp
uint32_t source;
uint32_t target;
```

## Rationale

`VertexId` is useful at the public grid/query boundary.

Raw `uint32_t` is preferred inside traversal kernels because:

```text
it avoids wrapper conversion
it keeps adjacency arrays simple
it aligns directly with CSR storage
it reduces the chance of accidental object copies
```

## Conversion Boundary

Conversion happens at the boundary:

```cpp
uint32_t s = query.source.value;
uint32_t t = query.target.value;
```

No `VertexId` wrapper should appear inside BFS/DFS inner loops.

---

# 8. ReachabilityAnswer and BaselineStatus Definitions

Add these exact definitions to:

```text
include/hbrick/core/types.hpp
```

or:

```text
include/hbrick/core/status.hpp
```

## ReachabilityAnswer

```cpp
enum class ReachabilityAnswer : uint8_t {
    Unreachable = 0,
    Reachable = 1
};
```

## BaselineStatus

```cpp
enum class BaselineStatus : uint8_t {
    NotRun = 0,
    Completed,
    SkippedByPolicy,
    OutOfMemory,
    Failed
};
```

Do not use strings in hot query APIs.

String conversion helpers may be added for reporting only.

---

# 9. SVG Text Escaping

`SvgCanvas::text()` must XML-escape text content.

Escaping rules:

```text
&  -> &amp;
<  -> &lt;
>  -> &gt;
"  -> &quot;
'  -> &apos;
```

Create a private helper:

```cpp
static std::string escapeXml(std::string_view input);
```

or a free helper in:

```text
src/viz/svg_canvas.cpp
```

## Required Tests

Add tests for SVG escaping:

```text
"plain" remains "plain"
"a & b" becomes "a &amp; b"
"x < y" becomes "x &lt; y"
"x > y" becomes "x &gt; y"
quote and apostrophe are escaped
```

Do not trust raw labels or filenames to be SVG-safe.

---

# 10. Threading Policy

Initial implementation is strictly **single-threaded**.

## Rule

No algorithm in Stages 0–10 shall create threads.

No OpenMP.

No `std::async`.

No background workers.

No parallel algorithms.

Benchmarking is single-threaded for reproducibility.

## Future Rule

If multi-threaded benchmarking is later added:

```text
one GraphSearchScratch per thread
one QueryScratch per thread
no shared mutable scratch buffers
```

This must be documented before any parallel code is added.

---

# 11. Revised Initial File Structure

Replace the previous initial structure with the following corrections.

## Remove

```text
include/hbrick/bit/timestamp_marker.hpp
src/bit/timestamp_marker.cpp
```

## Add

```text
include/hbrick/graph/graph_search_scratch.hpp
src/graph/graph_search_scratch.cpp

include/hbrick/baselines/closure_matrix_builder.hpp
src/baselines/closure_matrix_builder.cpp
```

## Corrected Initial Structure Excerpt

```text
include/
  hbrick/
    bit/
      bit_vector.hpp
      bit_matrix.hpp
      boolean_closure.hpp

    graph/
      edge32.hpp
      csr_graph.hpp
      csr_graph_builder.hpp
      graph_search_scratch.hpp
      bfs.hpp
      dfs.hpp
      scc_decomposition.hpp
      condensation_graph.hpp
      dag_reachability.hpp

    baselines/
      baseline_status.hpp
      closure_matrix_builder.hpp
      csr_bfs_baseline.hpp
      csr_dfs_baseline.hpp
      scc_dag_search_baseline.hpp
      scc_dag_closure_baseline.hpp
      full_closure_baseline.hpp
```

---

# 12. Revised Stage 0 Requirements

Stage 0 must explicitly set up:

```text
C++20
Google Test
CTest
target-based CMake
warnings
warnings-as-errors option
single-threaded build assumptions
```

## Stage 0 Acceptance Criteria

```text
cmake --preset dev succeeds
cmake --build --preset dev succeeds
ctest --preset dev --output-on-failure succeeds
one placeholder Google Test executable runs
no H-BRICK tile/summary/index files are created
no Boost/LEMON dependency is introduced yet
```

---

# 13. Revised Stage 5: BFS/DFS and GraphSearchScratch

Stage 5 shall implement:

```text
Bfs
Dfs
GraphSearchScratch
unit tests
```

## Required Files

```text
include/hbrick/graph/graph_search_scratch.hpp
src/graph/graph_search_scratch.cpp

include/hbrick/graph/bfs.hpp
src/graph/bfs.cpp

include/hbrick/graph/dfs.hpp
src/graph/dfs.cpp

tests/unit/test_bfs_dfs.cpp
```

## Acceptance

```text
BFS and DFS agree on known graphs
BFS and DFS agree on random small graphs
DFS is iterative, not recursive
reachable() allocates nothing
GraphSearchScratch handles mark overflow by clearing visited_mark_
```

---

# 14. Revised Stage 6: BitVector, BitMatrix, BooleanClosure

Stage 6 shall implement only pure bit logic.

## Required Files

```text
include/hbrick/bit/bit_vector.hpp
src/bit/bit_vector.cpp

include/hbrick/bit/bit_matrix.hpp
src/bit/bit_matrix.cpp

include/hbrick/bit/boolean_closure.hpp
src/bit/boolean_closure.cpp

tests/unit/test_bit_vector.cpp
tests/unit/test_bit_matrix.cpp
tests/unit/test_boolean_closure.cpp
```

## Explicitly Forbidden in Stage 6

```text
No CsrGraph include in bit headers.
No graph-to-matrix factory inside BooleanClosure.
No memory-limit policy inside BooleanClosure.
No baseline code.
```

## Acceptance

```text
BitVector tests pass
BitMatrix tests pass
BooleanClosure tests pass on manually constructed matrices
non-multiple-of-64 columns tested
zero-size matrix behavior tested
```

---

# 15. New Stage 6B: Closure Matrix Builder

Add a separate stage after Stage 6.

## Stage 6B Goal

Connect CSR graphs to BitMatrix closure safely, with memory guards.

## Required Files

```text
include/hbrick/baselines/closure_matrix_builder.hpp
src/baselines/closure_matrix_builder.cpp
tests/unit/test_closure_matrix_builder.cpp
```

## Acceptance

```text
memory estimate correct
small graph reflexive adjacency correct
large graph skipped or rejected by policy
identity entries set
edge entries set
no graph dependency in hbrick_bit
```

---

# 16. Revised Stage 7: SCC and Condensation DAG

Stage 7 may now use `ClosureMatrixBuilder` for SCC-DAG closure.

Acceptance must include:

```text
SCC-DAG closure checks memory before allocation
SCC-DAG search agrees with BFS
SCC-DAG closure agrees with BFS when completed
SCC-DAG closure returns SkippedByPolicy if memory estimate exceeds limit
```

---

# 17. Codex Context Feeding Rule

Do not paste the whole specification into Codex at every step.

Recommended workflow:

1. Save the main specification as:

```text
docs/codex_implementation_spec.md
```

2. Save this addendum as:

```text
docs/codex_safety_addendum.md
```

3. In every Codex task, say:

```text
Reference docs/codex_implementation_spec.md and docs/codex_safety_addendum.md for global rules. Execute only the task below.
```

4. Then paste the specific task template for the current stage.

This avoids context dilution and prevents the model from losing the most important local constraints.

---

# 18. Final Codex Task Template Amendment

Every Codex task must include this additional line:

```text
Testing framework:
    Use Google Test only.
```

Every task involving random generation must include:

```text
Randomness:
    Use std::mt19937_64 seeded exactly once from the provided seed.
    Enumerate grid adjacency pairs in row-major order.
    Do not use std::default_random_engine or std::random_device.
```

Every task involving graph traversal must include:

```text
Scratch:
    Use GraphSearchScratch.
    Do not allocate inside reachable(), bfs(), dfs(), or query().
    Use visited_mark stamping.
    Handle mark overflow by clearing visited_mark_ and resetting the mark to 1.
```

Every task involving SVG must include:

```text
SVG:
    SvgCanvas::text must XML-escape text content.
```

Every task involving closure allocation must include:

```text
Closure memory:
    Estimate matrix memory before allocation.
    Return SkippedByPolicy or throw a preprocessing-level exception if the configured memory limit is exceeded.
```

---

# 19. Final Readiness Statement

After applying this addendum, the specification is ready for Codex-assisted implementation of Stages 0–10.

Do not begin Stage 11 or any H-BRICK contribution-critical implementation until:

```text
base-tile port rules are finalized
flat port graph construction is specified
BRICK-Search pseudocode is finalized
H-BRICK parent composition pseudocode is finalized
H-BRICK query propagation pseudocode is finalized
at least one 2×2 tile composition example is hand-verified
expected matrices are written down
```

Until then, Codex should build only the infrastructure, baselines, correctness harness, and visualization foundation.
