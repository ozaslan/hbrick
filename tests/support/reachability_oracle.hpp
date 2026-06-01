#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace hbrick::test_support {

[[nodiscard]] ReachabilityAnswer bfsReference(
    const CsrGraph& graph,
    uint32_t source,
    uint32_t target,
    GraphSearchScratch& scratch
);

[[nodiscard]] CsrGraph buildGridGraph(
    const PassableGrid& grid,
    GridEdgeConversionMode mode,
    RandomAsymmetricParams params = {}
);

struct BaselineOracleResult {
    std::string name;
    BaselineStatus status = BaselineStatus::NotRun;
    uint32_t mismatches = 0U;
};

[[nodiscard]] std::vector<BaselineOracleResult> runAllBaselinesAgainstBfs(
    const CsrGraph& graph,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

[[nodiscard]] uint32_t countMismatchesAgainstBfs(
    const CsrGraph& graph,
    const std::function<ReachabilityAnswer(uint32_t, uint32_t)>& query
);

void expectAllBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

}  // namespace hbrick::test_support
