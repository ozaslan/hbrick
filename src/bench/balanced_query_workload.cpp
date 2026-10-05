#include "hbrick/bench/balanced_query_workload.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

#include "hbrick/bench/reachability_benchmark_util.hpp"
#include "hbrick/core/vertex_id.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint32_t manhattanDiameter(const MazeLayout& layout) noexcept {
    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    const uint32_t dx = width == 0U ? 0U : width - 1U;
    const uint32_t dy = height == 0U ? 0U : height - 1U;
    return dx + dy;
}

[[nodiscard]] uint32_t manhattanDistance(
    const MazeLayout& layout,
    const uint32_t grid_a,
    const uint32_t grid_b
) noexcept {
    const GridCoord a = layout.coordFromVertex(VertexId{grid_a});
    const GridCoord b = layout.coordFromVertex(VertexId{grid_b});
    const uint32_t dx = a.x >= b.x ? a.x - b.x : b.x - a.x;
    const uint32_t dy = a.y >= b.y ? a.y - b.y : b.y - a.y;
    return dx + dy;
}

[[nodiscard]] QueryRangeClass classifyRange(
    const uint32_t distance,
    const uint32_t diameter,
    const double local_fraction,
    const double medium_fraction
) noexcept {
    if (diameter == 0U) {
        return QueryRangeClass::Local;
    }
    const double scaled = static_cast<double>(distance) / static_cast<double>(diameter);
    if (scaled < local_fraction) {
        return QueryRangeClass::Local;
    }
    if (scaled < medium_fraction) {
        return QueryRangeClass::Medium;
    }
    return QueryRangeClass::Global;
}

[[nodiscard]] uint32_t bucketIndex(
    const QueryRangeClass range,
    const uint8_t polarity
) noexcept {
    return static_cast<uint32_t>(range) * 2U + (polarity == 1U ? 0U : 1U);
}

}  // namespace

const char* queryRangeClassLabel(const QueryRangeClass range) noexcept {
    switch (range) {
        case QueryRangeClass::Local:
            return "local";
        case QueryRangeClass::Medium:
            return "medium";
        case QueryRangeClass::Global:
            return "global";
    }
    return "unknown";
}

BalancedWorkload generateBalancedWorkload(
    const MazeLayout& layout,
    const CsrGraph& compact,
    const PassableVertexMap& map,
    const BalancedWorkloadSpec& spec
) {
    BalancedWorkload workload{};
    workload.manhattan_diameter = manhattanDiameter(layout);

    const uint32_t total = spec.query_count;
    const uint32_t base = total / 6U;
    const uint32_t remainder = total % 6U;
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        workload.requested_per_bucket[bucket] = base + (bucket < remainder ? 1U : 0U);
    }

    const std::span<const uint32_t> passable = map.passableGridVertices();
    if (passable.empty() || compact.numVertices() == 0U || total == 0U) {
        workload.complete = !spec.fail_if_shortfall || total == 0U;
        workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
        return workload;
    }

    std::vector<uint32_t> sources(passable.begin(), passable.end());
    std::mt19937_64 rng(spec.seed);
    std::shuffle(sources.begin(), sources.end(), rng);

    std::vector<ReachabilityQueryPair> buckets[6];
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        buckets[bucket].reserve(workload.requested_per_bucket[bucket]);
    }

    std::vector<uint32_t> small_targets;
    if (passable.size() <= 512U) {
        small_targets.assign(passable.begin(), passable.end());
        std::shuffle(small_targets.begin(), small_targets.end(), rng);
    }
    std::uniform_int_distribution<std::size_t> target_dist(0U, passable.size() - 1U);

    GraphSearchScratch scratch(compact.numVertices());
    auto buckets_full = [&]() -> bool {
        for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
            if (buckets[bucket].size() < workload.requested_per_bucket[bucket]) {
                return false;
            }
        }
        return true;
    };

    uint32_t sources_tried = 0U;
    constexpr uint32_t kMaxSourcesToTry = 4096U;

    for (const uint32_t grid_source : sources) {
        if (buckets_full() || ++sources_tried >= kMaxSourcesToTry) {
            break;
        }
        const uint32_t compact_source = map.toCompact(grid_source);
        if (compact_source == kInvalidVertexId
            || compact_source >= compact.numVertices()) {
            continue;
        }

        const uint32_t mark = scratch.nextMark();
        std::vector<uint32_t>& visited = scratch.visitedMark();
        std::vector<uint32_t>& queue = scratch.queue();
        queue.clear();
        visited[compact_source] = mark;
        queue.push_back(compact_source);
        std::size_t head = 0U;
        while (head < queue.size()) {
            const uint32_t vertex = queue[head];
            ++head;
            for (const uint32_t neighbor : compact.outNeighbors(vertex)) {
                if (visited[neighbor] == mark) {
                    continue;
                }
                visited[neighbor] = mark;
                queue.push_back(neighbor);
            }
        }

        const uint32_t max_targets_to_test = (passable.size() <= 512U)
            ? static_cast<uint32_t>(passable.size())
            : std::min<uint32_t>(static_cast<uint32_t>(passable.size()), 512U);

        for (uint32_t attempt = 0U; attempt < max_targets_to_test; ++attempt) {
            if (buckets_full()) {
                break;
            }
            const uint32_t grid_target = (passable.size() <= 512U)
                ? small_targets[attempt]
                : passable[target_dist(rng)];

            const uint32_t compact_target = map.toCompact(grid_target);
            if (compact_target == kInvalidVertexId
                || compact_target >= compact.numVertices()) {
                continue;
            }

            const QueryRangeClass range = classifyRange(
                manhattanDistance(layout, grid_source, grid_target),
                workload.manhattan_diameter,
                spec.local_fraction,
                spec.medium_fraction
            );
            const uint8_t polarity =
                visited[compact_target] == mark ? 1U : 0U;
            const uint32_t bucket = bucketIndex(range, polarity);
            if (buckets[bucket].size() >= workload.requested_per_bucket[bucket]) {
                continue;
            }

            ReachabilityQueryPair pair{};
            pair.source = grid_source;
            pair.target = grid_target;
            pair.range_class = static_cast<uint8_t>(range);
            pair.polarity = polarity;
            buckets[bucket].push_back(pair);
        }
    }

    workload.complete = true;
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        workload.filled_per_bucket[bucket] = static_cast<uint32_t>(buckets[bucket].size());
        if (workload.filled_per_bucket[bucket] < workload.requested_per_bucket[bucket]) {
            workload.complete = false;
        }
        workload.pairs.insert(
            workload.pairs.end(),
            buckets[bucket].begin(),
            buckets[bucket].end()
        );
    }
    if (spec.fail_if_shortfall && !workload.complete) {
        workload.pairs.clear();
    }
    workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
    return workload;
}

bool writeBalancedWorkloadCsv(
    const std::filesystem::path& path,
    const BalancedWorkload& workload,
    std::string& error_message
) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        error_message = "Failed to create workload directory: " + error.message();
        return false;
    }

    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error_message = "Failed to write " + path.string();
        return false;
    }
    output << "source,target,range,polarity\n";
    for (const ReachabilityQueryPair& pair : workload.pairs) {
        const QueryRangeClass range =
            pair.range_class <= 2U
                ? static_cast<QueryRangeClass>(pair.range_class)
                : QueryRangeClass::Local;
        output << pair.source << ',' << pair.target << ','
               << queryRangeClassLabel(range) << ','
               << (pair.polarity == 1U ? "reachable" : "unreachable") << '\n';
    }
    if (!output.good()) {
        error_message = "Failed to write " + path.string();
        return false;
    }
    return true;
}

bool readBalancedWorkloadCsv(
    const std::filesystem::path& path,
    BalancedWorkload& workload,
    std::string& error_message
) {
    workload = BalancedWorkload{};
    std::ifstream input(path);
    if (!input.is_open()) {
        error_message = "Failed to open " + path.string();
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        error_message = "Workload CSV is empty: " + path.string();
        return false;
    }

    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t first = line.find(',');
        const std::size_t second = first == std::string::npos
            ? std::string::npos
            : line.find(',', first + 1U);
        const std::size_t third = second == std::string::npos
            ? std::string::npos
            : line.find(',', second + 1U);
        if (first == std::string::npos || second == std::string::npos
            || third == std::string::npos) {
            error_message = "Malformed workload row in " + path.string();
            return false;
        }

        ReachabilityQueryPair pair{};
        try {
            pair.source = static_cast<uint32_t>(std::stoul(line.substr(0U, first)));
            pair.target = static_cast<uint32_t>(
                std::stoul(line.substr(first + 1U, second - first - 1U))
            );
        } catch (...) {
            error_message = "Malformed workload endpoints in " + path.string();
            return false;
        }
        const std::string range = line.substr(second + 1U, third - second - 1U);
        const std::string polarity = line.substr(third + 1U);
        if (range == "local") {
            pair.range_class = 0U;
        } else if (range == "medium") {
            pair.range_class = 1U;
        } else if (range == "global") {
            pair.range_class = 2U;
        } else {
            error_message = "Unknown range class in " + path.string();
            return false;
        }
        if (polarity == "reachable") {
            pair.polarity = 1U;
        } else if (polarity == "unreachable") {
            pair.polarity = 0U;
        } else {
            error_message = "Unknown polarity in " + path.string();
            return false;
        }
        workload.pairs.push_back(pair);
        const uint32_t bucket = bucketIndex(
            static_cast<QueryRangeClass>(pair.range_class),
            pair.polarity
        );
        ++workload.filled_per_bucket[bucket];
    }

    workload.complete = true;
    workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
    return true;
}

PolaritySplitWorkload generatePolaritySplitWorkload(
    const CsrGraph& compact,
    const PassableVertexMap& map,
    const PolaritySplitWorkloadSpec& spec
) {
    PolaritySplitWorkload workload{};
    workload.requested_positive = spec.positive_count;
    workload.requested_negative = spec.negative_count;

    const std::span<const uint32_t> passable = map.passableGridVertices();
    const uint32_t total = spec.positive_count + spec.negative_count;
    if (passable.empty() || compact.numVertices() == 0U || total == 0U) {
        workload.complete = !spec.fail_if_shortfall || total == 0U;
        workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
        return workload;
    }

    std::vector<uint32_t> sources(passable.begin(), passable.end());
    std::mt19937_64 rng(spec.seed);
    std::shuffle(sources.begin(), sources.end(), rng);

    std::vector<ReachabilityQueryPair> positives;
    std::vector<ReachabilityQueryPair> negatives;
    positives.reserve(spec.positive_count);
    negatives.reserve(spec.negative_count);

    GraphSearchScratch scratch(compact.numVertices());
    std::uniform_int_distribution<std::size_t> vertex_dist(
        0U,
        passable.size() - 1U
    );

    uint32_t fully_reachable_sources = 0U;
    constexpr uint32_t kGiveUpIfStronglyConnected = 64U;

    for (const uint32_t grid_source : sources) {
        if (positives.size() >= spec.positive_count
            && negatives.size() >= spec.negative_count) {
            break;
        }
        const uint32_t compact_source = map.toCompact(grid_source);
        if (compact_source == kInvalidVertexId
            || compact_source >= compact.numVertices()) {
            continue;
        }

        const uint32_t mark = scratch.nextMark();
        std::vector<uint32_t>& visited = scratch.visitedMark();
        std::vector<uint32_t>& queue = scratch.queue();
        queue.clear();
        visited[compact_source] = mark;
        queue.push_back(compact_source);
        std::size_t head = 0U;
        while (head < queue.size()) {
            const uint32_t vertex = queue[head];
            ++head;
            for (const uint32_t neighbor : compact.outNeighbors(vertex)) {
                if (visited[neighbor] == mark) {
                    continue;
                }
                visited[neighbor] = mark;
                queue.push_back(neighbor);
            }
        }

        const uint32_t reachable_count = static_cast<uint32_t>(queue.size());
        if (reachable_count >= compact.numVertices()) {
            ++fully_reachable_sources;
            if (spec.negative_count > 0U
                && negatives.size() < spec.negative_count
                && fully_reachable_sources >= kGiveUpIfStronglyConnected
                && positives.size() >= spec.positive_count) {
                break;
            }
        }

        auto try_sample = [&](const uint8_t want_polarity) {
            for (uint32_t attempt = 0U; attempt < 64U; ++attempt) {
                const uint32_t grid_target = passable[vertex_dist(rng)];
                const uint32_t compact_target = map.toCompact(grid_target);
                if (compact_target == kInvalidVertexId
                    || compact_target >= compact.numVertices()) {
                    continue;
                }
                if (want_polarity == 1U && compact_target == compact_source) {
                    continue;
                }
                const uint8_t polarity =
                    visited[compact_target] == mark ? 1U : 0U;
                if (polarity != want_polarity) {
                    continue;
                }
                ReachabilityQueryPair pair{};
                pair.source = grid_source;
                pair.target = grid_target;
                pair.range_class = static_cast<uint8_t>(QueryRangeClass::Global);
                pair.polarity = polarity;
                if (want_polarity == 1U) {
                    positives.push_back(pair);
                } else {
                    negatives.push_back(pair);
                }
                return;
            }
        };

        if (positives.size() < spec.positive_count && reachable_count > 1U) {
            try_sample(1U);
        }
        if (negatives.size() < spec.negative_count
            && reachable_count < compact.numVertices()) {
            try_sample(0U);
        }
    }

    workload.filled_positive = static_cast<uint32_t>(positives.size());
    workload.filled_negative = static_cast<uint32_t>(negatives.size());
    workload.complete =
        workload.filled_positive >= spec.positive_count
        && workload.filled_negative >= spec.negative_count;

    if (spec.fail_if_shortfall && !workload.complete) {
        workload.pairs.clear();
        workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
        return workload;
    }

    const std::size_t mixed = std::max(positives.size(), negatives.size());
    workload.pairs.reserve(positives.size() + negatives.size());
    for (std::size_t index = 0U; index < mixed; ++index) {
        if (index < positives.size()) {
            workload.pairs.push_back(positives[index]);
        }
        if (index < negatives.size()) {
            workload.pairs.push_back(negatives[index]);
        }
    }
    workload.pair_list_hash = hashReachabilityQueryPairs(workload.pairs);
    return workload;
}

}  // namespace hbrick
