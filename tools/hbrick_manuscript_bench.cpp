/**
 * @file hbrick_manuscript_bench.cpp
 * @brief Complete, headless benchmark suite reproducing the empirical tables
 *        and figure datasets of the H-BRICK manuscript.
 *
 * Stages (--stage all | comma-separated list):
 *  - graph_stats       -> graph_and_calibration.csv (map topology & calibration ladder)
 *  - param_sweep       -> param_sweep_results.csv (HBrickSkipLift b x g & flat BrickSearch b sweep)
 *  - threeway          -> threeway_b24_g4_results.csv, threeway_b24_g4_results_raw.csv
 *  - external          -> lifecycle_results.csv (BFS, DFS, SCC-DAG, O'Reach, GRAIL, 2-hop, BRICK variants)
 *  - batch_multisample -> batch_multisample.csv (10-draw alternating-order batch comparison)
 *  - polarity_and_v06  -> polarity_split.csv, v06_sensitivity.csv (R=1/R=0 split & 160-instance gradient sweep)
 *  - closure_kernels   -> closure_kernel_matrix.csv, closure_tile_ablation.csv (Warshall vs. M4RM)
 */

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unistd.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/grail_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/baselines/oreach_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/baselines/two_hop_baseline.hpp"
#include "hbrick/bench/balanced_query_workload.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/core/vertex_id.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/io/recipe.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kTimingChunks = 128U;

struct BenchArgs {
    std::string stage = "all";
    std::filesystem::path recipes_dir = "recipes";
    std::filesystem::path datasets_root = "datasets/movingai";
    std::filesystem::path out_dir = "campaigns/manuscript";
    uint32_t b = 24U;
    uint32_t g = 4U;
    uint32_t query_count = 8192U;
    uint32_t warmup_queries = 128U;
    uint64_t pair_seed = 0xBEEFCAFEULL;
    std::vector<uint32_t> k_values{4U, 16U, 64U};
    uint32_t batch_samples = 10U;
    uint32_t batch_repeats = 11U;
    uint32_t batch_warmup = 2U;
    double memory_gib = 2.5;
    uint32_t timeout_sec = 300U;
    uint32_t shard_index = 0U;
    uint32_t shard_count = 1U;
    bool resume = false;
    bool quick = false;
};

class SplitMix64Rng {
public:
    explicit SplitMix64Rng(const uint64_t seed) noexcept : state_(seed) {}

    uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    uint32_t uniform(const uint32_t bound) noexcept {
        return bound > 0U ? static_cast<uint32_t>(next() % bound) : 0U;
    }

private:
    uint64_t state_ = 0U;
};

struct DistStats {
    double mean = 0.0;
    double sd = 0.0;
    double min_v = 0.0;
    double q1 = 0.0;
    double median = 0.0;
    double q3 = 0.0;
    double max_v = 0.0;
    double qps = 0.0;
};

[[nodiscard]] double quantileSorted(
    const std::vector<double>& sorted,
    const double p
) noexcept {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1U) {
        return sorted[0];
    }
    const double pos = p * static_cast<double>(sorted.size() - 1U);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

[[nodiscard]] DistStats computeDistStats(std::vector<double> vals) {
    DistStats s{};
    if (vals.empty()) {
        return s;
    }
    std::sort(vals.begin(), vals.end());
    s.min_v = vals.front();
    s.max_v = vals.back();
    s.q1 = quantileSorted(vals, 0.25);
    s.median = quantileSorted(vals, 0.50);
    s.q3 = quantileSorted(vals, 0.75);
    double sum = 0.0;
    for (const double v : vals) {
        sum += v;
    }
    s.mean = sum / static_cast<double>(vals.size());
    if (s.mean > 0.0) {
        s.qps = 1e6 / s.mean;
    }
    if (vals.size() > 1U) {
        double sq = 0.0;
        for (const double v : vals) {
            const double d = v - s.mean;
            sq += d * d;
        }
        s.sd = std::sqrt(sq / static_cast<double>(vals.size() - 1U));
    }
    return s;
}

void raiseStackLimitIfNeeded() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    struct rlimit rl {};
    if (getrlimit(RLIMIT_STACK, &rl) != 0) {
        return;
    }
    constexpr rlim_t kMinStackBytes = static_cast<rlim_t>(512ULL * 1024ULL * 1024ULL);
    if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= kMinStackBytes) {
        return;
    }
    if (rl.rlim_max == RLIM_INFINITY) {
        struct rlimit unlim = rl;
        unlim.rlim_cur = RLIM_INFINITY;
        if (setrlimit(RLIMIT_STACK, &unlim) == 0) {
            return;
        }
    }
    const rlim_t target = (rl.rlim_max == RLIM_INFINITY)
        ? kMinStackBytes
        : std::min(kMinStackBytes, rl.rlim_max);
    if (target > rl.rlim_cur) {
        rl.rlim_cur = target;
        (void)setrlimit(RLIMIT_STACK, &rl);
    }
#endif
}

[[nodiscard]] uint64_t totalSystemRamBytes() noexcept {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
    }
    return 4ULL * 1024ULL * 1024ULL * 1024ULL;
}

[[nodiscard]] uint64_t currentProcessRssBytes() noexcept {
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) {
        return 0ULL;
    }
    uint64_t size_pages = 0ULL;
    uint64_t resident_pages = 0ULL;
    statm >> size_pages >> resident_pages;
    const long page_size = sysconf(_SC_PAGESIZE);
    return resident_pages * (page_size > 0 ? static_cast<uint64_t>(page_size) : 4096ULL);
}

[[nodiscard]] uint64_t computeMemoryCapBytes(const double memory_gib) noexcept {
    const uint64_t total_sys_ram = totalSystemRamBytes();
    const uint64_t initial_rss = currentProcessRssBytes();
    uint64_t cap = static_cast<uint64_t>(memory_gib * 1024.0 * 1024.0 * 1024.0);
    if (total_sys_ram <= static_cast<uint64_t>(4.5 * 1024.0 * 1024.0 * 1024.0)) {
        constexpr uint64_t kHeadroomBytes = static_cast<uint64_t>(1.2 * 1024.0 * 1024.0 * 1024.0);
        const uint64_t max_allowed = total_sys_ram > kHeadroomBytes ? (total_sys_ram - kHeadroomBytes) : 0ULL;
        const uint64_t safe_budget = max_allowed > initial_rss ? (max_allowed - initial_rss) : 0ULL;
        cap = std::min(cap, safe_budget);
    }
    return cap;
}

[[nodiscard]] std::string currentUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

[[nodiscard]] std::string paperMapIdFromRecipeLabel(const std::string& label) {
    if (label.size() < 3U) {
        return {};
    }
    const std::string prefix = label.substr(0U, 3U);
    if (prefix == "M03") return "M1";
    if (prefix == "M04") return "M2";
    if (prefix == "M05") return "M3";
    if (prefix == "M06") return "M4";
    if (prefix == "M02") return "M5";
    if (prefix == "M01") return "M6";
    if (prefix == "M07") return "M7";
    if (prefix == "M09") return "M8";
    return {};
}

[[nodiscard]] std::string regimeFromRecipeLabel(const std::string& label) {
    if (label.size() >= 7U && label[3] == '_' && label[4] == 'V') {
        return label.substr(4U, 3U);
    }
    return "UNK";
}

[[nodiscard]] int paperMapOrder(const std::string& paper_id) noexcept {
    if (paper_id.size() == 2U && paper_id[0] == 'M' && paper_id[1] >= '1' && paper_id[1] <= '8') {
        return static_cast<int>(paper_id[1] - '0');
    }
    return 99;
}

struct ActiveRecipeEntry {
    std::string paper_id;
    std::string regime;
    std::filesystem::path path;
    hbrick::tools::Recipe recipe;
};

[[nodiscard]] std::vector<ActiveRecipeEntry> loadActive48Recipes(
    const std::filesystem::path& recipes_dir
) {
    std::vector<ActiveRecipeEntry> entries;
    const auto files = hbrick::tools::listRecipes(recipes_dir);
    for (const auto& f : files) {
        const auto rec_opt = hbrick::tools::loadRecipe(f);
        if (!rec_opt.has_value()) {
            continue;
        }
        const std::string label = rec_opt->label.empty() ? f.stem().string() : rec_opt->label;
        const std::string pid = paperMapIdFromRecipeLabel(label);
        if (pid.empty()) {
            continue;
        }
        ActiveRecipeEntry e{};
        e.paper_id = pid;
        e.regime = regimeFromRecipeLabel(label);
        e.path = f;
        e.recipe = *rec_opt;
        if (e.recipe.label.empty()) {
            e.recipe.label = label;
        }
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(), [](const ActiveRecipeEntry& lhs, const ActiveRecipeEntry& rhs) {
        const int oa = paperMapOrder(lhs.paper_id);
        const int ob = paperMapOrder(rhs.paper_id);
        if (oa != ob) {
            return oa < ob;
        }
        return lhs.regime < rhs.regime;
    });
    return entries;
}

[[nodiscard]] std::vector<ActiveRecipeEntry> selectShard(
    const std::vector<ActiveRecipeEntry>& all,
    const uint32_t shard_index,
    const uint32_t shard_count
) {
    if (shard_count <= 1U) {
        return all;
    }
    std::vector<ActiveRecipeEntry> slice;
    for (size_t idx = 0U; idx < all.size(); ++idx) {
        if (static_cast<uint32_t>(idx % shard_count) == shard_index) {
            slice.push_back(all[idx]);
        }
    }
    return slice;
}

[[nodiscard]] std::filesystem::path resolveMapPath(
    const std::filesystem::path& datasets_root,
    const hbrick::tools::Recipe& recipe
) {
    const std::filesystem::path p = datasets_root / recipe.set_name / "maps" / recipe.map_name;
    if (std::filesystem::exists(p)) {
        return p;
    }
    const std::filesystem::path ext = datasets_root / "extracted" / recipe.set_name / "maps" / recipe.map_name;
    if (std::filesystem::exists(ext)) {
        return ext;
    }
    return datasets_root / recipe.map_name;
}

[[nodiscard]] hbrick::RandomAsymmetricParams orientationFromRecipe(const hbrick::tools::Recipe& recipe) noexcept {
    hbrick::RandomAsymmetricParams op{};
    op.seed = recipe.seed;
    op.p_bidirectional = static_cast<long double>(recipe.p_bidirectional);
    op.p_one_way = static_cast<long double>(recipe.p_one_way);
    op.p_edge_drop = 0.0L;
    op.gradient_angle_degrees = static_cast<double>(recipe.gradient_angle_degrees);
    op.p_against_gradient = static_cast<long double>(recipe.p_against_gradient);
    return op;
}

[[nodiscard]] std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        fields.push_back(item);
    }
    if (!line.empty() && line.back() == ',') {
        fields.emplace_back();
    }
    return fields;
}

[[nodiscard]] bool csvNeedsHeader(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    return std::filesystem::file_size(path, ec) == 0ULL;
}

template <typename Fn>
void readExistingCsvRows(const std::filesystem::path& path, Fn&& on_row) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (header) {
            header = false;
            continue;
        }
        on_row(splitCsvLine(line));
    }
}

[[nodiscard]] std::vector<uint32_t> sampleDistinctVertices(
    SplitMix64Rng& rng,
    const std::vector<uint32_t>& universe,
    const uint32_t count
) {
    std::vector<uint32_t> out;
    const uint32_t n = static_cast<uint32_t>(universe.size());
    if (count == 0U || n == 0U) {
        return out;
    }
    const uint32_t target = std::min(count, n);
    out.reserve(target);
    if (target * 4U >= n) {
        std::vector<uint8_t> taken(n, 0U);
        while (out.size() < target) {
            const uint32_t idx = rng.uniform(n);
            if (taken[idx] == 0U) {
                taken[idx] = 1U;
                out.push_back(universe[idx]);
            }
        }
        return out;
    }
    while (out.size() < target) {
        const uint32_t cand = universe[rng.uniform(n)];
        if (std::find(out.begin(), out.end(), cand) == out.end()) {
            out.push_back(cand);
        }
    }
    return out;
}

template <typename QueryFn>
DistStats measureChunkedQueries(
    QueryFn&& query_fn,
    const uint32_t total_queries,
    std::vector<uint8_t>& answers_out
) {
    answers_out.resize(total_queries);
    if (total_queries == 0U) {
        return DistStats{};
    }
    std::vector<double> samples_us;
    samples_us.reserve(total_queries);

    const uint32_t chunk_size = std::max(1U, total_queries / kTimingChunks);
    const uint32_t num_chunks = (total_queries + chunk_size - 1U) / chunk_size;

    double total_us = 0.0;
    for (uint32_t c = 0U; c < num_chunks; ++c) {
        const uint32_t start_idx = c * chunk_size;
        const uint32_t end_idx = std::min(total_queries, (c + 1U) * chunk_size);
        const auto t0 = Clock::now();
        for (uint32_t pos = start_idx; pos < end_idx; ++pos) {
            answers_out[pos] = query_fn(pos);
        }
        const auto t1 = Clock::now();
        const double chunk_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        total_us += chunk_us;
        const double per_q = chunk_us / static_cast<double>(end_idx - start_idx);
        for (uint32_t pos = start_idx; pos < end_idx; ++pos) {
            samples_us.push_back(per_q);
        }
    }
    DistStats st = computeDistStats(std::move(samples_us));
    st.mean = total_us / static_cast<double>(total_queries);
    st.qps = st.mean > 0.0 ? (1e6 / st.mean) : 0.0;
    return st;
}

template <typename QueryFn>
DistStats measureChunkedSubset(
    QueryFn&& query_fn,
    const std::vector<uint32_t>& indices,
    std::vector<uint8_t>* answers_out = nullptr
) {
    if (indices.empty()) {
        return DistStats{};
    }
    if (answers_out != nullptr) {
        answers_out->assign(indices.size(), 0U);
    }
    constexpr size_t kChunk = 128U;
    std::vector<double> per_chunk_means;
    per_chunk_means.reserve((indices.size() + kChunk - 1U) / kChunk);

    uint8_t sink = 0U;
    double total_us = 0.0;
    for (size_t base = 0U; base < indices.size(); base += kChunk) {
        const size_t end = std::min(indices.size(), base + kChunk);
        const auto t0 = Clock::now();
        for (size_t pos = base; pos < end; ++pos) {
            const uint8_t ans = query_fn(indices[pos]);
            sink ^= ans;
            if (answers_out != nullptr) {
                (*answers_out)[pos] = ans;
            }
        }
        const auto t1 = Clock::now();
        const double chunk_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        total_us += chunk_us;
        per_chunk_means.push_back(chunk_us / static_cast<double>(end - base));
    }
    if (sink == 0xFFU) {
        std::cerr << "";
    }
    DistStats st = computeDistStats(std::move(per_chunk_means));
    st.mean = total_us / static_cast<double>(indices.size());
    st.qps = st.mean > 0.0 ? (1e6 / st.mean) : 0.0;
    return st;
}

[[nodiscard]] uint32_t markReachableFromVertex(
    const hbrick::CsrGraph& graph,
    const uint32_t src_compact,
    hbrick::GraphSearchScratch& scratch
) noexcept {
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    const uint32_t stamp = scratch.nextMark();
    queue.clear();
    visited[src_compact] = stamp;
    queue.push_back(src_compact);
    size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t u = queue[head++];
        for (const uint32_t v : graph.outNeighbors(u)) {
            if (visited[v] != stamp) {
                visited[v] = stamp;
                queue.push_back(v);
            }
        }
    }
    return stamp;
}

struct SharedWorkload {
    std::vector<uint32_t> timed_src_grid;
    std::vector<uint32_t> timed_dst_grid;
    std::vector<uint32_t> timed_src_compact;
    std::vector<uint32_t> timed_dst_compact;
    std::vector<uint32_t> warmup_src_grid;
    std::vector<uint32_t> warmup_dst_grid;
    std::vector<uint32_t> warmup_src_compact;
    std::vector<uint32_t> warmup_dst_compact;
    std::vector<uint8_t> oracle_answers;
    DistStats bfs_stats{};
};

[[nodiscard]] SharedWorkload buildSharedBalancedWorkloadAndOracle(
    const hbrick::MazeLayout& layout,
    const hbrick::CsrGraph& csr,
    const hbrick::PassableVertexMap& passable_map,
    const std::vector<uint32_t>& universe,
    const uint32_t query_count,
    const uint32_t warmup_queries,
    const uint64_t pair_seed
) {
    hbrick::BalancedWorkloadSpec workload_spec{};
    workload_spec.query_count = query_count;
    workload_spec.seed = pair_seed;
    workload_spec.fail_if_shortfall = false;

    hbrick::BalancedWorkload b_workload = hbrick::generateBalancedWorkload(
        layout, csr, passable_map, workload_spec
    );

    SplitMix64Rng topup_rng(pair_seed);
    while (b_workload.pairs.size() < query_count) {
        const uint32_t sg = universe[topup_rng.uniform(static_cast<uint32_t>(universe.size()))];
        const uint32_t dg = universe[topup_rng.uniform(static_cast<uint32_t>(universe.size()))];
        hbrick::ReachabilityQueryPair p{};
        p.source = sg;
        p.target = dg;
        b_workload.pairs.push_back(p);
    }

    SplitMix64Rng shuffle_rng(pair_seed ^ 0xA5A5A5A55A5A5A5AULL);
    for (size_t idx = b_workload.pairs.size(); idx > 1U; --idx) {
        const size_t j = shuffle_rng.uniform(static_cast<uint32_t>(idx));
        std::swap(b_workload.pairs[idx - 1U], b_workload.pairs[j]);
    }

    SharedWorkload w{};
    w.timed_src_grid.resize(query_count);
    w.timed_dst_grid.resize(query_count);
    w.timed_src_compact.resize(query_count);
    w.timed_dst_compact.resize(query_count);
    for (uint32_t idx = 0U; idx < query_count; ++idx) {
        const uint32_t sg = b_workload.pairs[idx].source;
        const uint32_t dg = b_workload.pairs[idx].target;
        w.timed_src_grid[idx] = sg;
        w.timed_dst_grid[idx] = dg;
        w.timed_src_compact[idx] = passable_map.toCompact(sg);
        w.timed_dst_compact[idx] = passable_map.toCompact(dg);
    }

    w.warmup_src_grid.resize(warmup_queries);
    w.warmup_dst_grid.resize(warmup_queries);
    w.warmup_src_compact.resize(warmup_queries);
    w.warmup_dst_compact.resize(warmup_queries);
    for (uint32_t idx = 0U; idx < warmup_queries; ++idx) {
        const uint32_t src_idx = idx % query_count;
        w.warmup_src_grid[idx] = w.timed_src_grid[src_idx];
        w.warmup_dst_grid[idx] = w.timed_dst_grid[src_idx];
        w.warmup_src_compact[idx] = w.timed_src_compact[src_idx];
        w.warmup_dst_compact[idx] = w.timed_dst_compact[src_idx];
    }

    hbrick::CsrBfsBaseline bfs;
    bfs.preprocess(csr);
    hbrick::GraphSearchScratch scratch(csr.numVertices());
    for (uint32_t idx = 0U; idx < warmup_queries; ++idx) {
        (void)bfs.query(w.warmup_src_compact[idx], w.warmup_dst_compact[idx], scratch);
    }
    w.bfs_stats = measureChunkedQueries(
        [&](const uint32_t pos) -> uint8_t {
            return bfs.query(w.timed_src_compact[pos], w.timed_dst_compact[pos], scratch)
                    == hbrick::ReachabilityAnswer::Reachable
                ? 1U
                : 0U;
        },
        query_count,
        w.oracle_answers
    );
    return w;
}

[[nodiscard]] uint32_t countMismatches(
    const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& oracle
) noexcept {
    uint32_t mm = 0U;
    const size_t n = std::min(actual.size(), oracle.size());
    for (size_t idx = 0U; idx < n; ++idx) {
        if (actual[idx] != oracle[idx]) {
            ++mm;
        }
    }
    return mm;
}

[[nodiscard]] double toMiB(const uint64_t bytes) noexcept {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

[[nodiscard]] const char* statusString(const hbrick::BaselineStatus st) noexcept {
    switch (st) {
        case hbrick::BaselineStatus::Completed:
            return "Completed";
        case hbrick::BaselineStatus::SkippedByPolicy:
            return "SkippedByPolicy";
        case hbrick::BaselineStatus::OutOfMemory:
            return "OutOfMemory";
        case hbrick::BaselineStatus::Timeout:
            return "Timeout";
        case hbrick::BaselineStatus::Failed:
            return "Failed";
        case hbrick::BaselineStatus::NotRun:
            return "NotRun";
    }
    return "Failed";
}

struct MethodEval {
    std::string method_name;
    std::string status = "Completed";
    double preprocess_ms = 0.0;
    uint64_t index_bytes = 0ULL;
    uint64_t scratch_bytes = 0ULL;
    uint64_t rss_delta_bytes = 0ULL;
    double mean_query_us = 0.0;
    double median_query_us = 0.0;
    double qps = 0.0;
    double speedup_vs_bfs = 0.0;
    uint32_t oracle_mismatches = 0U;
};

[[nodiscard]] MethodEval evalBrickSearch(
    const hbrick::DirectedGridGraph& grid_graph,
    const hbrick::MazeLayout& layout,
    const SharedWorkload& wl,
    const uint32_t tile_side,
    const uint64_t max_memory_bytes
) {
    MethodEval ev{};
    ev.method_name = "BrickSearch";
    const uint64_t rss_before = currentProcessRssBytes();
    const auto t0 = Clock::now();
    auto flat = std::make_unique<hbrick::BrickSearchBaseline>();
    flat->preprocess(grid_graph, layout, hbrick::TileSize{tile_side, tile_side}, max_memory_bytes);
    const auto t1 = Clock::now();
    const uint64_t rss_after = currentProcessRssBytes();

    ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ev.index_bytes = flat->indexStorageBytes();
    ev.scratch_bytes = flat->portScratch().memoryBytes();
    if (rss_after > rss_before) {
        ev.rss_delta_bytes = rss_after - rss_before;
    }

    if (flat->status() == hbrick::BaselineStatus::Completed) {
        for (size_t idx = 0U; idx < wl.warmup_src_grid.size(); ++idx) {
            (void)flat->query(wl.warmup_src_grid[idx], wl.warmup_dst_grid[idx]);
        }
        std::vector<uint8_t> ans;
        const DistStats st = measureChunkedQueries(
            [&](const uint32_t pos) -> uint8_t {
                return flat->query(wl.timed_src_grid[pos], wl.timed_dst_grid[pos])
                        == hbrick::ReachabilityAnswer::Reachable
                    ? 1U
                    : 0U;
            },
            static_cast<uint32_t>(wl.timed_src_grid.size()),
            ans
        );
        ev.mean_query_us = st.mean;
        ev.median_query_us = st.median;
        ev.qps = st.qps;
        ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
        ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
        if (ev.oracle_mismatches > 0U) {
            std::cerr << "FATAL: BrickSearch oracle mismatch count=" << ev.oracle_mismatches << "\n";
            std::exit(2);
        }
    } else {
        ev.status = statusString(flat->status());
    }
    return ev;
}

[[nodiscard]] MethodEval evalHBrick(
    const hbrick::DirectedGridGraph& grid_graph,
    const hbrick::MazeLayout& layout,
    const SharedWorkload& wl,
    const hbrick::HBrickConfig& cfg
) {
    MethodEval ev{};
    ev.method_name = "HBrick";
    const uint64_t rss_before = currentProcessRssBytes();
    const auto t0 = Clock::now();
    auto hb = std::make_unique<hbrick::HBrickBaseline>();
    hb->preprocess(grid_graph, layout, cfg);
    const auto t1 = Clock::now();
    const uint64_t rss_after = currentProcessRssBytes();

    ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ev.index_bytes = hb->indexStorageBytes();
    ev.scratch_bytes = hb->scratch().memoryBytes()
        + hb->portBfsScratch().memoryBytes()
        + hb->microBfsScratch().memoryBytes();
    if (rss_after > rss_before) {
        ev.rss_delta_bytes = rss_after - rss_before;
    }

    if (hb->status() == hbrick::BaselineStatus::Completed) {
        for (size_t idx = 0U; idx < wl.warmup_src_grid.size(); ++idx) {
            (void)hb->query(wl.warmup_src_grid[idx], wl.warmup_dst_grid[idx]);
        }
        std::vector<uint8_t> ans;
        const DistStats st = measureChunkedQueries(
            [&](const uint32_t pos) -> uint8_t {
                return hb->query(wl.timed_src_grid[pos], wl.timed_dst_grid[pos])
                        == hbrick::ReachabilityAnswer::Reachable
                    ? 1U
                    : 0U;
            },
            static_cast<uint32_t>(wl.timed_src_grid.size()),
            ans
        );
        ev.mean_query_us = st.mean;
        ev.median_query_us = st.median;
        ev.qps = st.qps;
        ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
        ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
        if (ev.oracle_mismatches > 0U) {
            std::cerr << "FATAL: HBrick oracle mismatch count=" << ev.oracle_mismatches << "\n";
            std::exit(2);
        }
    } else {
        ev.status = statusString(hb->status());
    }
    return ev;
}

[[nodiscard]] MethodEval evalHBrickSkipLift(
    const hbrick::DirectedGridGraph& grid_graph,
    const hbrick::MazeLayout& layout,
    const SharedWorkload& wl,
    const hbrick::HBrickConfig& cfg
) {
    MethodEval ev{};
    ev.method_name = "HBrickSkipLift";
    const uint64_t rss_before = currentProcessRssBytes();
    const auto t0 = Clock::now();
    auto skip = std::make_unique<hbrick::HBrickSkipLiftBaseline>();
    skip->preprocess(grid_graph, layout, cfg);
    const auto t1 = Clock::now();
    const uint64_t rss_after = currentProcessRssBytes();

    ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ev.index_bytes = skip->indexStorageBytes();
    ev.scratch_bytes = skip->scratchMemoryBytes();
    if (rss_after > rss_before) {
        ev.rss_delta_bytes = rss_after - rss_before;
    }

    if (skip->status() == hbrick::BaselineStatus::Completed) {
        for (size_t idx = 0U; idx < wl.warmup_src_grid.size(); ++idx) {
            (void)skip->query(wl.warmup_src_grid[idx], wl.warmup_dst_grid[idx]);
        }
        std::vector<uint8_t> ans;
        const DistStats st = measureChunkedQueries(
            [&](const uint32_t pos) -> uint8_t {
                return skip->query(wl.timed_src_grid[pos], wl.timed_dst_grid[pos])
                        == hbrick::ReachabilityAnswer::Reachable
                    ? 1U
                    : 0U;
            },
            static_cast<uint32_t>(wl.timed_src_grid.size()),
            ans
        );
        ev.mean_query_us = st.mean;
        ev.median_query_us = st.median;
        ev.qps = st.qps;
        ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
        ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
        if (ev.oracle_mismatches > 0U) {
            std::cerr << "FATAL: HBrickSkipLift oracle mismatch count=" << ev.oracle_mismatches << "\n";
            std::exit(2);
        }
    } else {
        ev.status = statusString(skip->status());
    }
    return ev;
}

// ============================================================================
// STAGE 1: Graph Topology & Calibration Density (Tables 4 & 5)
// ============================================================================
int runGraphStats(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path csv_path = args.out_dir / "graph_and_calibration.csv";

    std::unordered_set<std::string> done_labels;
    if (args.resume) {
        readExistingCsvRows(csv_path, [&](const std::vector<std::string>& f) {
            if (f.size() > 1U) {
                done_labels.insert(f[1]);
            }
        });
    }

    const bool write_header = !args.resume || csvNeedsHeader(csv_path);
    std::ofstream csv(csv_path, args.resume ? std::ios::app : std::ios::trunc);
    if (write_header) {
        csv << "paper_id,recipe_label,map_name,regime,width,height,vertices,geom_edges,directed_edges,"
            << "geom_edges_per_vertex,obstacle_pct,p_bi,theta_deg,p_back,num_sccs,scc_ratio,reach_density_pct,tc_gib\n";
    }

    std::cout << "[graph_stats] Computing map topology, SCC counts, and reachability density...\n";
    const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();
    for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
        const auto& entry = recipes[r_idx];
        if (args.resume && done_labels.count(entry.recipe.label) > 0U) {
            continue;
        }
        const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
        const auto loaded = hbrick::loadMovingAiMap(map_path);
        if (!loaded.ok()) {
            std::cerr << "Error loading map " << map_path << ": " << loaded.error << "\n";
            return 1;
        }
        const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
        const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
            layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
        );
        const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
        const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
        const auto u_span = passable_map.passableGridVertices();
        const uint32_t n_verts = csr.numVertices();

        // Undirected 4-neighbor passable edges (right + down)
        uint64_t m_geom = 0ULL;
        const uint32_t w = layout.width();
        const uint32_t h = layout.height();
        for (uint32_t y = 0U; y < h; ++y) {
            for (uint32_t x = 0U; x < w; ++x) {
                if (!layout.isPassable(x, y)) {
                    continue;
                }
                if (x + 1U < w && layout.isPassable(x + 1U, y)) {
                    ++m_geom;
                }
                if (y + 1U < h && layout.isPassable(x, y + 1U)) {
                    ++m_geom;
                }
            }
        }

        const uint64_t total_cells = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
        const double obstacle_pct = total_cells > 0ULL
            ? 100.0 * static_cast<double>(total_cells - n_verts) / static_cast<double>(total_cells)
            : 0.0;
        const double m_per_v = n_verts > 0U
            ? static_cast<double>(m_geom) / static_cast<double>(n_verts)
            : 0.0;
        const uint64_t n64 = static_cast<uint64_t>(n_verts);
        const uint64_t tc_bytes = (n64 * n64 + 7ULL) / 8ULL;
        const double tc_gib = static_cast<double>(tc_bytes) / (1024.0 * 1024.0 * 1024.0);

        hbrick::GraphSearchScratch scc_scratch(n_verts);
        hbrick::SccDagSearchBaseline scc;
        scc.preprocess(csr, scc_scratch);
        const uint32_t num_sccs = scc.numComponents();
        const double scc_ratio = n_verts > 0U
            ? static_cast<double>(num_sccs) / static_cast<double>(n_verts)
            : 0.0;

        // Estimate reachability density rho = |{(s,t): s != t, Reach(s,t)}| / (N*(N-1))
        uint32_t sample_budget = args.quick ? 64U : (scc_ratio > 0.1 ? 4096U : 256U);
        sample_budget = std::min(sample_budget, n_verts);
        std::vector<uint32_t> compact_universe(n_verts);
        std::iota(compact_universe.begin(), compact_universe.end(), 0U);
        SplitMix64Rng rng(0x9E3779B97F4A7C15ULL ^ entry.recipe.seed ^ static_cast<uint64_t>(r_idx));
        const std::vector<uint32_t> sampled = sampleDistinctVertices(rng, compact_universe, sample_budget);

        uint64_t reachable_pairs = 0ULL;
        hbrick::GraphSearchScratch bfs_scratch(n_verts);
        for (const uint32_t src_c : sampled) {
            (void)markReachableFromVertex(csr, src_c, bfs_scratch);
            const size_t reached_cnt = bfs_scratch.queue().size();
            if (reached_cnt > 1U) {
                reachable_pairs += static_cast<uint64_t>(reached_cnt - 1U);
            }
        }
        const double denom = static_cast<double>(sampled.size()) * static_cast<double>(n_verts > 1U ? (n_verts - 1U) : 1U);
        const double rho_pct = denom > 0.0 ? (100.0 * static_cast<double>(reachable_pairs) / denom) : 0.0;
        (void)u_span;

        csv << std::fixed
            << entry.paper_id << "," << entry.recipe.label << "," << entry.recipe.map_name << ","
            << entry.regime << "," << w << "," << h << "," << n_verts << ","
            << m_geom << "," << csr.numEdges() << ","
            << std::setprecision(4) << m_per_v << "," << std::setprecision(2) << obstacle_pct << ","
            << std::setprecision(2) << static_cast<double>(entry.recipe.p_bidirectional) << ","
            << std::setprecision(1) << static_cast<double>(entry.recipe.gradient_angle_degrees) << ","
            << std::setprecision(2) << static_cast<double>(entry.recipe.p_against_gradient) << ","
            << num_sccs << "," << std::setprecision(6) << scc_ratio << ","
            << std::setprecision(4) << rho_pct << "," << std::setprecision(4) << tc_gib << "\n";
        csv.flush();
    }
    return 0;
}

// ============================================================================
// STAGE 2: Hierarchy Parameter Sweep (Tables 6, 7, 8, 9)
// ============================================================================
int runParamSweep(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path csv_path = args.out_dir / "param_sweep_results.csv";

    std::unordered_set<std::string> done_keys;
    if (args.resume) {
        readExistingCsvRows(csv_path, [&](const std::vector<std::string>& f) {
            if (f.size() > 6U) {
                done_keys.insert(f[1] + "|" + f[3] + "|" + f[4] + "|" + f[5]);
            }
        });
    }

    const bool write_header = !args.resume || csvNeedsHeader(csv_path);
    std::ofstream csv(csv_path, args.resume ? std::ios::app : std::ios::trunc);
    if (write_header) {
        csv << "timestamp,recipe_label,map_name,b,g,method,status,preprocess_ms,index_mb,"
            << "scratch_mb,rss_delta_mb,mean_query_us,median_query_us,qps,speedup_vs_bfs,mismatches\n";
    }

    const std::vector<uint32_t> b_vals = args.quick
        ? std::vector<uint32_t>{16U, 24U}
        : std::vector<uint32_t>{4U, 8U, 16U, 24U, 32U, 48U, 64U, 96U};
    const std::vector<uint32_t> g_vals = args.quick
        ? std::vector<uint32_t>{2U, 4U}
        : std::vector<uint32_t>{2U, 4U, 8U};
    const uint32_t q_count = args.quick ? 512U : args.query_count;
    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);
    const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();

    std::cout << "[param_sweep] Running HBrickSkipLift (b x g) and flat BrickSearch (b) sweep...\n";
    for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
        const auto& entry = recipes[r_idx];
        bool all_done = args.resume;
        if (all_done) {
            for (const uint32_t bv : b_vals) {
                for (const uint32_t gv : g_vals) {
                    const std::string k1 = entry.recipe.label + "|" + std::to_string(bv) + "|" + std::to_string(gv) + "|HBrickSkipLift";
                    if (done_keys.count(k1) == 0U) {
                        all_done = false;
                        break;
                    }
                    if (gv == 4U) {
                        const std::string k2 = entry.recipe.label + "|" + std::to_string(bv) + "|4|BrickSearch";
                        if (done_keys.count(k2) == 0U) {
                            all_done = false;
                            break;
                        }
                    }
                }
                if (!all_done) {
                    break;
                }
            }
        }
        if (all_done) {
            continue;
        }

        const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
        const auto loaded = hbrick::loadMovingAiMap(map_path);
        if (!loaded.ok()) {
            std::cerr << "Error loading map " << map_path << ": " << loaded.error << "\n";
            return 1;
        }
        const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
        const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
            layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
        );
        const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
        const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
        const auto u_span = passable_map.passableGridVertices();
        const std::vector<uint32_t> universe(u_span.begin(), u_span.end());

        const SharedWorkload wl = buildSharedBalancedWorkloadAndOracle(
            layout, csr, passable_map, universe, q_count, args.warmup_queries, args.pair_seed
        );

        auto emitSweepRow = [&](const uint32_t bv, const uint32_t gv, const MethodEval& ev) {
            csv << currentUtcTimestamp() << ","
                << entry.recipe.label << "," << entry.recipe.map_name << ","
                << bv << "," << gv << "," << ev.method_name << "," << ev.status << ","
                << std::fixed << std::setprecision(3) << ev.preprocess_ms << ","
                << std::setprecision(4) << toMiB(ev.index_bytes) << ","
                << toMiB(ev.scratch_bytes) << ","
                << toMiB(ev.rss_delta_bytes) << ","
                << std::setprecision(3) << ev.mean_query_us << ","
                << ev.median_query_us << ","
                << std::setprecision(1) << ev.qps << ","
                << std::setprecision(2) << ev.speedup_vs_bfs << ","
                << ev.oracle_mismatches << "\n";
            csv.flush();
        };

        for (const uint32_t bv : b_vals) {
            for (const uint32_t gv : g_vals) {
                const std::string k_skip = entry.recipe.label + "|" + std::to_string(bv) + "|" + std::to_string(gv) + "|HBrickSkipLift";
                if (!args.resume || done_keys.count(k_skip) == 0U) {
                    hbrick::HBrickConfig cfg{};
                    cfg.base_tile_size = hbrick::TileSize{bv, bv};
                    cfg.group_size = hbrick::GroupSize{gv, gv};
                    cfg.max_depth = hbrick::kHBrickFullDepth;
                    cfg.max_memory_bytes = max_mem;
                    const MethodEval ev = evalHBrickSkipLift(grid_graph, layout, wl, cfg);
                    emitSweepRow(bv, gv, ev);
                }
                if (gv == 4U) {
                    const std::string k_flat = entry.recipe.label + "|" + std::to_string(bv) + "|4|BrickSearch";
                    if (!args.resume || done_keys.count(k_flat) == 0U) {
                        const MethodEval ev_flat = evalBrickSearch(grid_graph, layout, wl, bv, max_mem);
                        emitSweepRow(bv, 4U, ev_flat);
                    }
                }
            }
        }
        std::cout << "  [param_sweep] Completed " << entry.paper_id << " " << entry.regime << "\n";
    }
    return 0;
}

// ============================================================================
// STAGE 3: Matched Three-Way Ablation at (b=24, g=4) (Table 10)
// ============================================================================
int runThreeway(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path csv_path = args.out_dir / "threeway_b24_g4_results.csv";
    const std::filesystem::path raw_path = args.out_dir / "threeway_b24_g4_results_raw.csv";

    std::unordered_set<std::string> done_labels;
    if (args.resume) {
        std::unordered_set<std::string> keys;
        readExistingCsvRows(csv_path, [&](const std::vector<std::string>& f) {
            if (f.size() > 6U) {
                keys.insert(f[1] + "|" + f[6]);
            }
        });
        for (const auto& entry : recipes) {
            const std::string& lbl = entry.recipe.label;
            if (keys.count(lbl + "|CsrBFS") > 0U
                && keys.count(lbl + "|BrickSearch") > 0U
                && keys.count(lbl + "|HBrick") > 0U
                && keys.count(lbl + "|HBrickSkipLift") > 0U) {
                done_labels.insert(lbl);
            }
        }
    }

    const bool write_header = !args.resume || csvNeedsHeader(csv_path);
    const bool write_raw_header = !args.resume || csvNeedsHeader(raw_path);
    std::ofstream csv(csv_path, args.resume ? std::ios::app : std::ios::trunc);
    std::ofstream rcsv(raw_path, args.resume ? std::ios::app : std::ios::trunc);
    if (write_header) {
        csv << "timestamp,recipe_label,map_name,regime,b,g,method,status,preprocess_ms,index_mb,"
            << "scratch_mb,rss_delta_mb,mean_query_us,median_query_us,qps,speedup_vs_bfs,mismatches\n";
    }
    if (write_raw_header) {
        rcsv << "timestamp,recipe_label,map_name,regime,b,g,repeat,method,status,preprocess_ms,index_mb,"
             << "scratch_mb,mean_query_us,median_query_us,qps,speedup_vs_bfs,mismatches\n";
    }

    const uint32_t q_count = args.quick ? 512U : args.query_count;
    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);
    const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();

    std::cout << "[threeway] Running matched 3-pass cyclic ablation (BrickSearch, HBrick, HBrickSkipLift)...\n";
    for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
        const auto& entry = recipes[r_idx];
        if (args.resume && done_labels.count(entry.recipe.label) > 0U) {
            continue;
        }
        const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
        const auto loaded = hbrick::loadMovingAiMap(map_path);
        if (!loaded.ok()) {
            std::cerr << "Error loading map " << map_path << ": " << loaded.error << "\n";
            return 1;
        }
        const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
        const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
            layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
        );
        const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
        const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
        const auto u_span = passable_map.passableGridVertices();
        const std::vector<uint32_t> universe(u_span.begin(), u_span.end());

        const SharedWorkload wl = buildSharedBalancedWorkloadAndOracle(
            layout, csr, passable_map, universe, q_count, args.warmup_queries, args.pair_seed
        );

        hbrick::HBrickConfig cfg{};
        cfg.base_tile_size = hbrick::TileSize{args.b, args.b};
        cfg.group_size = hbrick::GroupSize{args.g, args.g};
        cfg.max_depth = hbrick::kHBrickFullDepth;
        cfg.max_memory_bytes = max_mem;

        constexpr uint32_t kRepeats = 3U;
        std::vector<MethodEval> samples[3];
        for (uint32_t rep = 0U; rep < kRepeats; ++rep) {
            for (uint32_t step = 0U; step < 3U; ++step) {
                const uint32_t slot = (rep + step) % 3U;
                if (slot == 0U) {
                    samples[0].push_back(evalBrickSearch(grid_graph, layout, wl, args.b, max_mem));
                } else if (slot == 1U) {
                    samples[1].push_back(evalHBrick(grid_graph, layout, wl, cfg));
                } else {
                    samples[2].push_back(evalHBrickSkipLift(grid_graph, layout, wl, cfg));
                }
            }
        }

        auto med3 = [](double v0, double v1, double v2) noexcept -> double {
            if (v0 > v1) std::swap(v0, v1);
            if (v1 > v2) std::swap(v1, v2);
            if (v0 > v1) std::swap(v0, v1);
            return v1;
        };

        std::vector<MethodEval> agg_rows;
        MethodEval bfs_ev{};
        bfs_ev.method_name = "CsrBFS";
        bfs_ev.mean_query_us = wl.bfs_stats.mean;
        bfs_ev.median_query_us = wl.bfs_stats.median;
        bfs_ev.qps = wl.bfs_stats.qps;
        bfs_ev.speedup_vs_bfs = 1.0;
        agg_rows.push_back(bfs_ev);

        for (uint32_t slot = 0U; slot < 3U; ++slot) {
            MethodEval agg = samples[slot][0];
            agg.preprocess_ms = med3(samples[slot][0].preprocess_ms, samples[slot][1].preprocess_ms, samples[slot][2].preprocess_ms);
            agg.mean_query_us = med3(samples[slot][0].mean_query_us, samples[slot][1].mean_query_us, samples[slot][2].mean_query_us);
            agg.median_query_us = med3(samples[slot][0].median_query_us, samples[slot][1].median_query_us, samples[slot][2].median_query_us);
            agg.qps = agg.mean_query_us > 0.0 ? (1e6 / agg.mean_query_us) : 0.0;
            agg.speedup_vs_bfs = agg.mean_query_us > 0.0 ? (wl.bfs_stats.mean / agg.mean_query_us) : 0.0;
            agg_rows.push_back(agg);
        }

        for (const auto& ev : agg_rows) {
            csv << currentUtcTimestamp() << ","
                << entry.recipe.label << "," << entry.recipe.map_name << "," << entry.regime << ","
                << args.b << "," << args.g << "," << ev.method_name << "," << ev.status << ","
                << std::fixed << std::setprecision(3) << ev.preprocess_ms << ","
                << std::setprecision(4) << toMiB(ev.index_bytes) << ","
                << toMiB(ev.scratch_bytes) << "," << toMiB(ev.rss_delta_bytes) << ","
                << std::setprecision(3) << ev.mean_query_us << "," << ev.median_query_us << ","
                << std::setprecision(1) << ev.qps << "," << std::setprecision(2) << ev.speedup_vs_bfs << ","
                << ev.oracle_mismatches << "\n";
        }
        csv.flush();

        for (uint32_t slot = 0U; slot < 3U; ++slot) {
            for (uint32_t rep = 0U; rep < kRepeats; ++rep) {
                const auto& ev = samples[slot][rep];
                rcsv << currentUtcTimestamp() << ","
                     << entry.recipe.label << "," << entry.recipe.map_name << "," << entry.regime << ","
                     << args.b << "," << args.g << "," << rep << "," << ev.method_name << "," << ev.status << ","
                     << std::fixed << std::setprecision(3) << ev.preprocess_ms << ","
                     << std::setprecision(4) << toMiB(ev.index_bytes) << ","
                     << toMiB(ev.scratch_bytes) << ","
                     << std::setprecision(3) << ev.mean_query_us << "," << ev.median_query_us << ","
                     << std::setprecision(1) << ev.qps << "," << std::setprecision(2) << ev.speedup_vs_bfs << ","
                     << ev.oracle_mismatches << "\n";
            }
        }
        rcsv.flush();
        std::cout << "  [threeway] Completed " << entry.paper_id << " " << entry.regime << "\n";
    }
    return 0;
}

// ============================================================================
// STAGE 4: External Baselines & Lifecycle Evaluation (Tables 14, 15, 16, Fig 6a, 7)
// ============================================================================
int runExternal(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path csv_path = args.out_dir / "lifecycle_results.csv";

    std::unordered_set<std::string> done_labels;
    if (args.resume) {
        std::unordered_set<std::string> keys;
        readExistingCsvRows(csv_path, [&](const std::vector<std::string>& f) {
            if (f.size() > 4U) {
                keys.insert(f[1] + "|" + f[4]);
            }
        });
        for (const auto& entry : recipes) {
            const std::string& lbl = entry.recipe.label;
            if (keys.count(lbl + "|CsrBFS") > 0U
                && keys.count(lbl + "|CsrDFS") > 0U
                && keys.count(lbl + "|SccDagSearch") > 0U
                && keys.count(lbl + "|OReach") > 0U
                && keys.count(lbl + "|Grail") > 0U
                && keys.count(lbl + "|TwoHop") > 0U
                && keys.count(lbl + "|BrickSearch") > 0U
                && keys.count(lbl + "|HBrick") > 0U
                && keys.count(lbl + "|HBrickSkipLift") > 0U) {
                done_labels.insert(lbl);
            }
        }
    }

    const bool write_header = !args.resume || csvNeedsHeader(csv_path);
    std::ofstream csv(csv_path, args.resume ? std::ios::app : std::ios::trunc);
    if (write_header) {
        csv << "timestamp,recipe_label,map_name,regime,method,status,preprocess_ms,index_mb,"
            << "scratch_mb,rss_delta_mb,mean_query_us,median_query_us,qps,speedup_vs_bfs,mismatches,"
            << "num_sccs,c_over_n\n";
    }

    const uint32_t q_count = args.quick ? 512U : args.query_count;
    const uint64_t total_sys_ram = totalSystemRamBytes();
    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);
    const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();

    std::cout << "[external] Evaluating all 9 methods at (b=" << args.b << ", g=" << args.g << ")...\n";
    for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
        const auto& entry = recipes[r_idx];
        if (args.resume && done_labels.count(entry.recipe.label) > 0U) {
            continue;
        }
        const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
        const auto loaded = hbrick::loadMovingAiMap(map_path);
        if (!loaded.ok()) {
            std::cerr << "Error loading map " << map_path << ": " << loaded.error << "\n";
            return 1;
        }
        const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
        const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
            layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
        );
        const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
        const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
        const auto u_span = passable_map.passableGridVertices();
        const std::vector<uint32_t> universe(u_span.begin(), u_span.end());

        const SharedWorkload wl = buildSharedBalancedWorkloadAndOracle(
            layout, csr, passable_map, universe, q_count, args.warmup_queries, args.pair_seed
        );

        std::vector<MethodEval> evals;
        MethodEval bfs_ev{};
        bfs_ev.method_name = "CsrBFS";
        bfs_ev.mean_query_us = wl.bfs_stats.mean;
        bfs_ev.median_query_us = wl.bfs_stats.median;
        bfs_ev.qps = wl.bfs_stats.qps;
        bfs_ev.speedup_vs_bfs = 1.0;
        evals.push_back(bfs_ev);

        // CsrDFS
        {
            MethodEval ev{};
            ev.method_name = "CsrDFS";
            hbrick::CsrDfsBaseline dfs;
            dfs.preprocess(csr);
            hbrick::GraphSearchScratch scratch(csr.numVertices());
            for (uint32_t i = 0U; i < args.warmup_queries; ++i) {
                (void)dfs.query(wl.warmup_src_compact[i], wl.warmup_dst_compact[i], scratch);
            }
            std::vector<uint8_t> ans;
            const DistStats st = measureChunkedQueries(
                [&](const uint32_t pos) -> uint8_t {
                    return dfs.query(wl.timed_src_compact[pos], wl.timed_dst_compact[pos], scratch)
                            == hbrick::ReachabilityAnswer::Reachable
                        ? 1U
                        : 0U;
                },
                q_count,
                ans
            );
            ev.scratch_bytes = scratch.memoryBytes();
            ev.mean_query_us = st.mean;
            ev.median_query_us = st.median;
            ev.qps = st.qps;
            ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
            ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
            evals.push_back(ev);
        }

        // SccDagSearch
        uint32_t num_sccs = 0U;
        double c_over_n = 0.0;
        {
            MethodEval ev{};
            ev.method_name = "SccDagSearch";
            hbrick::GraphSearchScratch scratch(csr.numVertices());
            const uint64_t rss_before = currentProcessRssBytes();
            const auto t0 = Clock::now();
            hbrick::SccDagSearchBaseline scc;
            scc.preprocess(csr, scratch);
            const auto t1 = Clock::now();
            const uint64_t rss_after = currentProcessRssBytes();

            ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            ev.index_bytes = scc.indexStorageBytes();
            ev.scratch_bytes = scratch.memoryBytes();
            if (rss_after > rss_before) {
                ev.rss_delta_bytes = rss_after - rss_before;
            }
            num_sccs = scc.numComponents();
            c_over_n = csr.numVertices() > 0U
                ? static_cast<double>(num_sccs) / static_cast<double>(csr.numVertices())
                : 0.0;

            if (scc.status() == hbrick::BaselineStatus::Completed) {
                for (uint32_t i = 0U; i < args.warmup_queries; ++i) {
                    (void)scc.query(wl.warmup_src_compact[i], wl.warmup_dst_compact[i], scratch);
                }
                std::vector<uint8_t> ans;
                const DistStats st = measureChunkedQueries(
                    [&](const uint32_t pos) -> uint8_t {
                        return scc.query(wl.timed_src_compact[pos], wl.timed_dst_compact[pos], scratch)
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    },
                    q_count,
                    ans
                );
                ev.mean_query_us = st.mean;
                ev.median_query_us = st.median;
                ev.qps = st.qps;
                ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
                ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
            } else {
                ev.status = statusString(scc.status());
            }
            evals.push_back(ev);
        }

        // OReach
        {
            MethodEval ev{};
            ev.method_name = "OReach";
            const uint64_t rss_before = currentProcessRssBytes();
            const auto t0 = Clock::now();
            auto oreach = std::make_unique<hbrick::OreachBaseline>();
            oreach->preprocess(csr, hbrick::OreachBaselineParams{}, max_mem);
            const auto t1 = Clock::now();
            const uint64_t rss_after = currentProcessRssBytes();

            ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            ev.index_bytes = oreach->indexStorageBytes();
            ev.scratch_bytes = static_cast<uint64_t>(csr.numVertices()) * sizeof(uint32_t) * 2ULL;
            if (rss_after > rss_before) {
                ev.rss_delta_bytes = rss_after - rss_before;
            }
            if (oreach->status() == hbrick::BaselineStatus::Completed) {
                hbrick::GraphSearchScratch scratch(csr.numVertices());
                for (uint32_t i = 0U; i < args.warmup_queries; ++i) {
                    (void)oreach->query(wl.warmup_src_compact[i], wl.warmup_dst_compact[i], scratch);
                }
                std::vector<uint8_t> ans;
                const DistStats st = measureChunkedQueries(
                    [&](const uint32_t pos) -> uint8_t {
                        return oreach->query(wl.timed_src_compact[pos], wl.timed_dst_compact[pos], scratch)
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    },
                    q_count,
                    ans
                );
                ev.mean_query_us = st.mean;
                ev.median_query_us = st.median;
                ev.qps = st.qps;
                ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
                ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
            } else {
                ev.status = statusString(oreach->status());
            }
            evals.push_back(ev);
        }

        // Grail
        {
            MethodEval ev{};
            ev.method_name = "Grail";
            const uint64_t rss_before = currentProcessRssBytes();
            const auto t0 = Clock::now();
            auto grail = std::make_unique<hbrick::GrailBaseline>();
            grail->preprocess(csr, hbrick::GrailBaselineParams{}, max_mem);
            const auto t1 = Clock::now();
            const uint64_t rss_after = currentProcessRssBytes();

            ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            ev.index_bytes = grail->labelStorageBytes();
            ev.scratch_bytes = static_cast<uint64_t>(csr.numVertices()) * sizeof(uint32_t);
            if (rss_after > rss_before) {
                ev.rss_delta_bytes = rss_after - rss_before;
            }
            if (grail->status() == hbrick::BaselineStatus::Completed) {
                hbrick::GraphSearchScratch scratch(csr.numVertices());
                for (uint32_t i = 0U; i < args.warmup_queries; ++i) {
                    (void)grail->query(wl.warmup_src_compact[i], wl.warmup_dst_compact[i], scratch);
                }
                std::vector<uint8_t> ans;
                const DistStats st = measureChunkedQueries(
                    [&](const uint32_t pos) -> uint8_t {
                        return grail->query(wl.timed_src_compact[pos], wl.timed_dst_compact[pos], scratch)
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    },
                    q_count,
                    ans
                );
                ev.mean_query_us = st.mean;
                ev.median_query_us = st.median;
                ev.qps = st.qps;
                ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
                ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
            } else {
                ev.status = statusString(grail->status());
            }
            evals.push_back(ev);
        }

        // TwoHop (memory & timeout guarded)
        {
            MethodEval ev{};
            ev.method_name = "TwoHop";
            try {
                hbrick::GraphSearchScratch scratch(csr.numVertices());
                const uint64_t cur_rss = currentProcessRssBytes();
                uint64_t twohop_budget = max_mem;
                if (total_sys_ram <= static_cast<uint64_t>(4.5 * 1024.0 * 1024.0 * 1024.0)) {
                    constexpr uint64_t kHeadroom = static_cast<uint64_t>(1.2 * 1024.0 * 1024.0 * 1024.0);
                    const uint64_t max_allowed = total_sys_ram > kHeadroom ? (total_sys_ram - kHeadroom) : 0ULL;
                    const uint64_t safe_budget = max_allowed > cur_rss ? (max_allowed - cur_rss) : 0ULL;
                    twohop_budget = std::min(twohop_budget, safe_budget);
                }
                if (twohop_budget < 64ULL * 1024ULL * 1024ULL) {
                    ev.status = "SkippedByPolicy";
                } else {
                    auto two_hop = std::make_unique<hbrick::TwoHopBaseline>();
                    const uint64_t rss_before = currentProcessRssBytes();
                    const auto t0 = Clock::now();
                    two_hop->preprocess(csr, scratch, twohop_budget, args.timeout_sec);
                    const auto t1 = Clock::now();
                    const uint64_t rss_after = currentProcessRssBytes();

                    ev.preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    ev.index_bytes = two_hop->labelStorageBytes();
                    if (rss_after > rss_before) {
                        ev.rss_delta_bytes = rss_after - rss_before;
                    }
                    if (two_hop->status() == hbrick::BaselineStatus::Completed) {
                        for (uint32_t i = 0U; i < args.warmup_queries; ++i) {
                            (void)two_hop->query(wl.warmup_src_compact[i], wl.warmup_dst_compact[i]);
                        }
                        std::vector<uint8_t> ans;
                        const DistStats st = measureChunkedQueries(
                            [&](const uint32_t pos) -> uint8_t {
                                return two_hop->query(wl.timed_src_compact[pos], wl.timed_dst_compact[pos])
                                        == hbrick::ReachabilityAnswer::Reachable
                                    ? 1U
                                    : 0U;
                            },
                            q_count,
                            ans
                        );
                        ev.mean_query_us = st.mean;
                        ev.median_query_us = st.median;
                        ev.qps = st.qps;
                        ev.speedup_vs_bfs = st.mean > 0.0 ? (wl.bfs_stats.mean / st.mean) : 0.0;
                        ev.oracle_mismatches = countMismatches(ans, wl.oracle_answers);
                    } else {
                        ev.status = statusString(two_hop->status());
                    }
                }
            } catch (const std::bad_alloc&) {
                ev.status = "OutOfMemory";
            }
            evals.push_back(ev);
        }

        // BrickSearch, HBrick, HBrickSkipLift at (b, g)
        hbrick::HBrickConfig cfg{};
        cfg.base_tile_size = hbrick::TileSize{args.b, args.b};
        cfg.group_size = hbrick::GroupSize{args.g, args.g};
        cfg.max_depth = hbrick::kHBrickFullDepth;
        cfg.max_memory_bytes = max_mem;

        evals.push_back(evalBrickSearch(grid_graph, layout, wl, args.b, max_mem));
        evals.push_back(evalHBrick(grid_graph, layout, wl, cfg));
        evals.push_back(evalHBrickSkipLift(grid_graph, layout, wl, cfg));

        for (const auto& ev : evals) {
            if (ev.oracle_mismatches > 0U) {
                std::cerr << "FATAL: Oracle mismatch in " << ev.method_name
                          << " on " << entry.recipe.label << ": " << ev.oracle_mismatches << "\n";
                return 2;
            }
        }
        for (const auto& ev : evals) {
            csv << currentUtcTimestamp() << ","
                << entry.recipe.label << "," << entry.recipe.map_name << "," << entry.regime << ","
                << ev.method_name << "," << ev.status << ","
                << std::fixed << std::setprecision(3) << ev.preprocess_ms << ","
                << std::setprecision(4) << toMiB(ev.index_bytes) << ","
                << toMiB(ev.scratch_bytes) << "," << toMiB(ev.rss_delta_bytes) << ","
                << std::setprecision(3) << ev.mean_query_us << "," << ev.median_query_us << ","
                << std::setprecision(1) << ev.qps << "," << std::setprecision(2) << ev.speedup_vs_bfs << ","
                << ev.oracle_mismatches << "," << num_sccs << "," << std::setprecision(6) << c_over_n << "\n";
        }
        csv.flush();
        std::cout << "  [external] Completed " << entry.paper_id << " " << entry.regime << "\n";
    }
    return 0;
}

// ============================================================================
// STAGE 5: Multi-Sample Alternating-Order Batch Sweep (Tables 11, 12, 13, Fig 6b)
// ============================================================================
int runBatchMultisample(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path csv_path = args.out_dir / "batch_multisample.csv";

    const std::vector<uint32_t> k_vals = args.quick
        ? std::vector<uint32_t>{4U, 16U}
        : args.k_values;
    const uint32_t n_samples = args.quick ? 3U : args.batch_samples;
    const uint32_t n_repeats = args.quick ? 3U : args.batch_repeats;
    const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();
    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);

    constexpr size_t kNumMethods = 6U;
    const char* const method_names[kNumMethods] = {
        "batch-h-brick",
        "scalar-h-brick",
        "bfs-per-source",
        "scc-dag-per-source",
        "scc-dag-dedup",
        "oreach"
    };

    std::unordered_set<std::string> done_keys;
    if (args.resume) {
        readExistingCsvRows(csv_path, [&](const std::vector<std::string>& f) {
            if (f.size() > 8U) {
                done_keys.insert(f[1] + "|" + f[4] + "|" + f[8]);
            }
        });
    }

    const bool write_header = !args.resume || csvNeedsHeader(csv_path);
    std::ofstream csv(csv_path, args.resume ? std::ios::app : std::ios::trunc);
    if (write_header) {
        csv << "paper_id,recipe_label,map_name,regime,k,pair_count,num_samples,repeats_per_sample,method,"
            << "median_batch_us,q1_batch_us,q3_batch_us,mean_batch_us,sd_batch_us,min_batch_us,max_batch_us,"
            << "median_ns_per_pair,q1_ns_per_pair,q3_ns_per_pair,"
            << "batch_speedup_over_method_median,batch_speedup_over_method_q1,batch_speedup_over_method_q3,mismatches\n";
    }

    std::cout << "[batch_multisample] Running multi-sample alternating-order batch benchmark...\n";
    for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
        const auto& entry = recipes[r_idx];
        bool all_k_done = args.resume;
        if (all_k_done) {
            for (const uint32_t kv : k_vals) {
                for (size_t m = 0U; m < kNumMethods; ++m) {
                    if (done_keys.count(entry.recipe.label + "|" + std::to_string(kv) + "|" + method_names[m]) == 0U) {
                        all_k_done = false;
                        break;
                    }
                }
                if (!all_k_done) {
                    break;
                }
            }
        }
        if (all_k_done) {
            continue;
        }

        const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
        const auto loaded = hbrick::loadMovingAiMap(map_path);
        if (!loaded.ok()) {
            std::cerr << "Error loading map " << map_path << ": " << loaded.error << "\n";
            return 1;
        }
        const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
        const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
            layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
        );
        const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
        const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
        const auto u_span = passable_map.passableGridVertices();
        const std::vector<uint32_t> universe(u_span.begin(), u_span.end());

        hbrick::HBrickConfig hb_cfg{};
        hb_cfg.base_tile_size = hbrick::TileSize{args.b, args.b};
        hb_cfg.group_size = hbrick::GroupSize{args.g, args.g};
        hb_cfg.max_depth = hbrick::kHBrickFullDepth;
        hb_cfg.max_memory_bytes = max_mem;

        hbrick::HBrickSkipLiftBaseline hb;
        hb.preprocess(grid_graph, layout, hb_cfg);

        hbrick::GraphSearchScratch scc_pre_scratch(csr.numVertices());
        hbrick::SccDagSearchBaseline scc;
        scc.preprocess(csr, scc_pre_scratch);
        const hbrick::CsrGraph& cond_dag = scc.condensationDag();

        hbrick::OreachBaseline oreach;
        oreach.preprocess(csr, hbrick::OreachBaselineParams{}, max_mem);

        hbrick::GraphSearchScratch bfs_scratch(csr.numVertices());
        hbrick::GraphSearchScratch dag_scratch(std::max(1U, cond_dag.numVertices()));
        hbrick::GraphSearchScratch oreach_scratch(csr.numVertices());

        for (const uint32_t kv : k_vals) {
            bool this_k_done = args.resume;
            if (this_k_done) {
                for (size_t m = 0U; m < kNumMethods; ++m) {
                    if (done_keys.count(entry.recipe.label + "|" + std::to_string(kv) + "|" + method_names[m]) == 0U) {
                        this_k_done = false;
                        break;
                    }
                }
            }
            if (this_k_done) {
                continue;
            }
            if (kv == 0U || kv > universe.size() || !hb.prepareBatch(kv, kv)) {
                continue;
            }
            const size_t pairs = static_cast<size_t>(kv) * static_cast<size_t>(kv);

            std::vector<std::vector<double>> sample_medians_us(kNumMethods);
            std::vector<std::vector<double>> paired_speedup(kNumMethods);
            uint32_t total_mismatches[kNumMethods] = {0U, 0U, 0U, 0U, 0U, 0U};

            std::vector<uint32_t> src_c(kv, 0U);
            std::vector<uint32_t> dst_c(kv, 0U);
            std::vector<uint32_t> src_comp(kv, 0U);
            std::vector<uint32_t> dst_comp(kv, 0U);
            std::vector<uint32_t> unique_src_comp;
            unique_src_comp.reserve(kv);

            std::vector<uint8_t> out_buf[kNumMethods];
            for (size_t m = 0U; m < kNumMethods; ++m) {
                out_buf[m].assign(pairs, 0U);
            }

            for (uint32_t s_idx = 0U; s_idx < n_samples; ++s_idx) {
                SplitMix64Rng rng(
                    entry.recipe.seed
                    ^ (0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(kv) + 1ULL))
                    ^ args.pair_seed
                    ^ (0xD1B54A32D192ED03ULL * static_cast<uint64_t>(s_idx))
                );
                const std::vector<uint32_t> src_g = sampleDistinctVertices(rng, universe, kv);
                const std::vector<uint32_t> dst_g = sampleDistinctVertices(rng, universe, kv);
                for (uint32_t idx = 0U; idx < kv; ++idx) {
                    src_c[idx] = passable_map.toCompact(src_g[idx]);
                    dst_c[idx] = passable_map.toCompact(dst_g[idx]);
                }

                auto runMethodOnce = [&](const size_t m) {
                    uint8_t* const out = out_buf[m].data();
                    if (m == 0U) {
                        (void)hb.batchQuery(src_g, dst_g, out_buf[0]);
                    } else if (m == 1U) {
                        for (uint32_t si = 0U; si < kv; ++si) {
                            for (uint32_t di = 0U; di < kv; ++di) {
                                out[si * kv + di] =
                                    hb.query(src_g[si], dst_g[di]) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                            }
                        }
                    } else if (m == 2U) {
                        for (uint32_t si = 0U; si < kv; ++si) {
                            const uint32_t stamp = markReachableFromVertex(csr, src_c[si], bfs_scratch);
                            const auto& vis = bfs_scratch.visitedMark();
                            for (uint32_t di = 0U; di < kv; ++di) {
                                out[si * kv + di] = (vis[dst_c[di]] == stamp) ? 1U : 0U;
                            }
                        }
                    } else if (m == 3U) {
                        for (uint32_t idx = 0U; idx < kv; ++idx) {
                            src_comp[idx] = scc.componentOf(src_c[idx]);
                            dst_comp[idx] = scc.componentOf(dst_c[idx]);
                        }
                        for (uint32_t si = 0U; si < kv; ++si) {
                            const uint32_t stamp = markReachableFromVertex(cond_dag, src_comp[si], dag_scratch);
                            const auto& vis = dag_scratch.visitedMark();
                            for (uint32_t di = 0U; di < kv; ++di) {
                                out[si * kv + di] = (vis[dst_comp[di]] == stamp) ? 1U : 0U;
                            }
                        }
                    } else if (m == 4U) {
                        unique_src_comp.clear();
                        for (uint32_t idx = 0U; idx < kv; ++idx) {
                            src_comp[idx] = scc.componentOf(src_c[idx]);
                            dst_comp[idx] = scc.componentOf(dst_c[idx]);
                            if (std::find(unique_src_comp.begin(), unique_src_comp.end(), src_comp[idx]) == unique_src_comp.end()) {
                                unique_src_comp.push_back(src_comp[idx]);
                            }
                        }
                        for (const uint32_t uc : unique_src_comp) {
                            const uint32_t stamp = markReachableFromVertex(cond_dag, uc, dag_scratch);
                            const auto& vis = dag_scratch.visitedMark();
                            for (uint32_t si = 0U; si < kv; ++si) {
                                if (src_comp[si] != uc) {
                                    continue;
                                }
                                for (uint32_t di = 0U; di < kv; ++di) {
                                    out[si * kv + di] = (vis[dst_comp[di]] == stamp) ? 1U : 0U;
                                }
                            }
                        }
                    } else {
                        for (uint32_t si = 0U; si < kv; ++si) {
                            for (uint32_t di = 0U; di < kv; ++di) {
                                out[si * kv + di] =
                                    oreach.query(src_c[si], dst_c[di], oreach_scratch) == hbrick::ReachabilityAnswer::Reachable
                                        ? 1U
                                        : 0U;
                            }
                        }
                    }
                };

                for (uint32_t w = 0U; w < args.batch_warmup; ++w) {
                    for (size_t m = 0U; m < kNumMethods; ++m) {
                        runMethodOnce((m + w + s_idx) % kNumMethods);
                    }
                }
                for (size_t m = 0U; m < kNumMethods; ++m) {
                    if (m == 2U) {
                        continue;
                    }
                    total_mismatches[m] += countMismatches(out_buf[m], out_buf[2]);
                }

                std::vector<double> rep_times_us[kNumMethods];
                for (size_t m = 0U; m < kNumMethods; ++m) {
                    rep_times_us[m].reserve(n_repeats);
                }

                for (uint32_t rep = 0U; rep < n_repeats; ++rep) {
                    for (size_t step = 0U; step < kNumMethods; ++step) {
                        const size_t shifted = ((s_idx + rep) % 2U == 0U)
                            ? ((step + s_idx + rep) % kNumMethods)
                            : ((kNumMethods - 1U - step + s_idx + rep) % kNumMethods);
                        const auto t0 = Clock::now();
                        runMethodOnce(shifted);
                        const auto t1 = Clock::now();
                        rep_times_us[shifted].push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                    }
                }

                for (size_t m = 0U; m < kNumMethods; ++m) {
                    const DistStats r_st = computeDistStats(rep_times_us[m]);
                    sample_medians_us[m].push_back(r_st.median);
                }
                const double batch_med = sample_medians_us[0].back();
                if (batch_med > 0.0) {
                    for (size_t m = 0U; m < kNumMethods; ++m) {
                        paired_speedup[m].push_back(sample_medians_us[m].back() / batch_med);
                    }
                }
            }

            for (size_t m = 0U; m < kNumMethods; ++m) {
                if (total_mismatches[m] > 0U) {
                    std::cerr << "FATAL: Batch oracle mismatch in " << method_names[m] << "\n";
                    return 2;
                }
            }

            for (size_t m = 0U; m < kNumMethods; ++m) {
                const DistStats st = computeDistStats(sample_medians_us[m]);
                const DistStats sp_st = computeDistStats(paired_speedup[m]);
                const double scale_ns = 1000.0 / static_cast<double>(pairs);
                csv << std::fixed << std::setprecision(4)
                    << entry.paper_id << "," << entry.recipe.label << "," << entry.recipe.map_name << ","
                    << entry.regime << "," << kv << "," << pairs << "," << n_samples << "," << n_repeats << ","
                    << method_names[m] << ","
                    << st.median << "," << st.q1 << "," << st.q3 << ","
                    << st.mean << "," << st.sd << "," << st.min_v << "," << st.max_v << ","
                    << (st.median * scale_ns) << "," << (st.q1 * scale_ns) << "," << (st.q3 * scale_ns) << ","
                    << sp_st.median << "," << sp_st.q1 << "," << sp_st.q3 << ","
                    << total_mismatches[m] << "\n";
            }
            csv.flush();
        }
        std::cout << "  [batch_multisample] Completed " << entry.paper_id << " " << entry.regime << "\n";
    }
    return 0;
}

// ============================================================================
// STAGE 6: Polarity Split & V06 Sensitivity Sweep (Tables 17 & 18)
// ============================================================================
int runPolarityAndV06(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path split_path = args.out_dir / "polarity_split.csv";
    const std::filesystem::path v06_path = args.out_dir / "v06_sensitivity.csv";

    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);
    const uint32_t q_count = args.quick ? 512U : args.query_count;

    // 6A: Reachable vs. Unreachable split
    {
        std::unordered_set<std::string> done_split;
        if (args.resume) {
            std::unordered_set<std::string> keys;
            readExistingCsvRows(split_path, [&](const std::vector<std::string>& f) {
                if (f.size() > 7U) {
                    keys.insert(f[1] + "|" + f[7]);
                }
            });
            for (const auto& entry : recipes) {
                const std::string& lbl = entry.recipe.label;
                if (keys.count(lbl + "|SccDagSearch") > 0U
                    && keys.count(lbl + "|OReach") > 0U
                    && keys.count(lbl + "|HBrickSkipLift") > 0U) {
                    done_split.insert(lbl);
                }
            }
        }

        const bool write_header = !args.resume || csvNeedsHeader(split_path);
        std::ofstream split_csv(split_path, args.resume ? std::ios::app : std::ios::trunc);
        if (write_header) {
            split_csv << "paper_id,recipe_label,map_name,regime,vertices,num_sccs,scc_ratio,method,"
                      << "preprocess_ms,index_bytes,n_total,n_reach,n_unreach,"
                      << "overall_mean_us,overall_median_us,"
                      << "reach_mean_us,reach_median_us,reach_q1_us,reach_q3_us,"
                      << "unreach_mean_us,unreach_median_us,unreach_q1_us,unreach_q3_us,mismatches\n";
        }

        std::cout << "[polarity_and_v06] Running Reachable vs. Unreachable split...\n";
        const size_t max_rec = args.quick ? std::min<size_t>(2U, recipes.size()) : recipes.size();
        for (size_t r_idx = 0U; r_idx < max_rec; ++r_idx) {
            const auto& entry = recipes[r_idx];
            if (args.resume && done_split.count(entry.recipe.label) > 0U) {
                continue;
            }
            const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
            const auto loaded = hbrick::loadMovingAiMap(map_path);
            if (!loaded.ok()) {
                return 1;
            }
            const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
            const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
                layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
            );
            const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
            const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
            const auto u_span = passable_map.passableGridVertices();
            const std::vector<uint32_t> universe(u_span.begin(), u_span.end());
            hbrick::GraphSearchScratch scratch(csr.numVertices());

            hbrick::HBrickConfig hb_cfg{};
            hb_cfg.base_tile_size = hbrick::TileSize{args.b, args.b};
            hb_cfg.group_size = hbrick::GroupSize{args.g, args.g};
            hb_cfg.max_depth = hbrick::kHBrickFullDepth;
            hb_cfg.max_memory_bytes = max_mem;

            hbrick::HBrickSkipLiftBaseline hb;
            const auto t_hb0 = Clock::now();
            hb.preprocess(grid_graph, layout, hb_cfg);
            const double hb_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_hb0).count();

            hbrick::SccDagSearchBaseline scc;
            const auto t_scc0 = Clock::now();
            scc.preprocess(csr, scratch);
            const double scc_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_scc0).count();
            const uint32_t num_sccs = scc.numComponents();
            const double scc_ratio = csr.numVertices() > 0U
                ? static_cast<double>(num_sccs) / static_cast<double>(csr.numVertices())
                : 0.0;

            hbrick::OreachBaseline oreach;
            const auto t_or0 = Clock::now();
            oreach.preprocess(csr, hbrick::OreachBaselineParams{}, max_mem);
            const double or_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_or0).count();

            const SharedWorkload wl = buildSharedBalancedWorkloadAndOracle(
                layout, csr, passable_map, universe, q_count, args.warmup_queries, args.pair_seed
            );

            std::vector<uint32_t> all_idx(q_count);
            std::iota(all_idx.begin(), all_idx.end(), 0U);
            std::vector<uint32_t> reach_idx;
            std::vector<uint32_t> unreach_idx;
            for (uint32_t idx = 0U; idx < q_count; ++idx) {
                if (wl.oracle_answers[idx] != 0U) {
                    reach_idx.push_back(idx);
                } else {
                    unreach_idx.push_back(idx);
                }
            }

            for (uint32_t idx = 0U; idx < args.warmup_queries; ++idx) {
                (void)scc.query(wl.warmup_src_compact[idx], wl.warmup_dst_compact[idx], scratch);
            }
            std::vector<uint8_t> scc_ans;
            const DistStats scc_all = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return scc.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                all_idx, &scc_ans
            );
            const DistStats scc_r = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return scc.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                reach_idx
            );
            const DistStats scc_u = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return scc.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                unreach_idx
            );

            for (uint32_t idx = 0U; idx < args.warmup_queries; ++idx) {
                (void)oreach.query(wl.warmup_src_compact[idx], wl.warmup_dst_compact[idx], scratch);
            }
            std::vector<uint8_t> or_ans;
            const DistStats or_all = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return oreach.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                all_idx, &or_ans
            );
            const DistStats or_r = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return oreach.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                reach_idx
            );
            const DistStats or_u = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return oreach.query(wl.timed_src_compact[idx], wl.timed_dst_compact[idx], scratch)
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                unreach_idx
            );

            for (uint32_t idx = 0U; idx < args.warmup_queries; ++idx) {
                (void)hb.query(wl.warmup_src_grid[idx], wl.warmup_dst_grid[idx]);
            }
            std::vector<uint8_t> hb_ans;
            const DistStats hb_all = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return hb.query(wl.timed_src_grid[idx], wl.timed_dst_grid[idx])
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                all_idx, &hb_ans
            );
            const DistStats hb_r = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return hb.query(wl.timed_src_grid[idx], wl.timed_dst_grid[idx])
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                reach_idx
            );
            const DistStats hb_u = measureChunkedSubset(
                [&](const uint32_t idx) -> uint8_t {
                    return hb.query(wl.timed_src_grid[idx], wl.timed_dst_grid[idx])
                            == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                },
                unreach_idx
            );

            const uint32_t mm_scc = countMismatches(scc_ans, wl.oracle_answers);
            const uint32_t mm_or = countMismatches(or_ans, wl.oracle_answers);
            const uint32_t mm_hb = countMismatches(hb_ans, wl.oracle_answers);
            if (mm_scc > 0U || mm_or > 0U || mm_hb > 0U) {
                std::cerr << "FATAL: Polarity split oracle mismatch!\n";
                return 2;
            }

            auto emitSplit = [&](const char* name, const double pre_ms, const uint64_t idx_bytes,
                                 const DistStats& st_all, const DistStats& st_r, const DistStats& st_u,
                                 const uint32_t mis) {
                split_csv << std::fixed << std::setprecision(4)
                          << entry.paper_id << "," << entry.recipe.label << "," << entry.recipe.map_name << ","
                          << entry.regime << "," << csr.numVertices() << "," << num_sccs << "," << scc_ratio << ","
                          << name << "," << pre_ms << "," << idx_bytes << ","
                          << q_count << "," << reach_idx.size() << "," << unreach_idx.size() << ","
                          << st_all.mean << "," << st_all.median << ","
                          << st_r.mean << "," << st_r.median << "," << st_r.q1 << "," << st_r.q3 << ","
                          << st_u.mean << "," << st_u.median << "," << st_u.q1 << "," << st_u.q3 << ","
                          << mis << "\n";
            };
            emitSplit("SccDagSearch", scc_pre_ms, scc.indexStorageBytes(), scc_all, scc_r, scc_u, mm_scc);
            emitSplit("OReach", or_pre_ms, oreach.indexStorageBytes(), or_all, or_r, or_u, mm_or);
            emitSplit("HBrickSkipLift", hb_pre_ms, hb.indexStorageBytes(), hb_all, hb_r, hb_u, mm_hb);
            split_csv.flush();
        }
    }

    // 6B: V06 Gradient-Flow (theta, p_back) Sensitivity
    {
        auto keyV06 = [](const std::string& pid, const double th, const double pb) {
            return pid + "|" + std::to_string(std::llround(th * 10.0)) + "|" + std::to_string(std::llround(pb * 1000.0));
        };
        std::unordered_set<std::string> done_v06;
        if (args.resume) {
            readExistingCsvRows(v06_path, [&](const std::vector<std::string>& f) {
                if (f.size() > 3U) {
                    done_v06.insert(keyV06(f[0], std::stod(f[2]), std::stod(f[3])));
                }
            });
        }

        const bool write_header = !args.resume || csvNeedsHeader(v06_path);
        std::ofstream v06_csv(v06_path, args.resume ? std::ios::app : std::ios::trunc);
        if (write_header) {
            v06_csv << "paper_id,map_name,theta_deg,p_back,vertices,edges,num_sccs,scc_ratio,"
                    << "empirical_reach_frac,n_reach,n_unreach,"
                    << "scc_pre_ms,scc_mean_us,scc_reach_us,scc_unreach_us,"
                    << "oreach_pre_ms,oreach_mb,oreach_mean_us,oreach_reach_us,oreach_unreach_us,"
                    << "hbrick_pre_ms,hbrick_mb,hbrick_mean_us,hbrick_reach_us,hbrick_unreach_us,"
                    << "hbrick_vs_oreach_overall,hbrick_vs_oreach_reach,hbrick_vs_oreach_unreach,mismatches\n";
        }

        const std::vector<double> thetas = args.quick
            ? std::vector<double>{0.0, 45.0}
            : std::vector<double>{0.0, 22.5, 45.0, 90.0};
        const std::vector<double> p_backs = args.quick
            ? std::vector<double>{0.00, 0.05}
            : std::vector<double>{0.00, 0.02, 0.05, 0.10, 0.20};

        std::vector<ActiveRecipeEntry> v06_recipes;
        for (const auto& entry : recipes) {
            if (entry.regime == "V06") {
                v06_recipes.push_back(entry);
            }
        }
        const size_t max_maps = args.quick ? std::min<size_t>(2U, v06_recipes.size()) : v06_recipes.size();
        std::cout << "[polarity_and_v06] Running V06 (theta, p_back) gradient sensitivity sweep...\n";

        for (size_t m_idx = 0U; m_idx < max_maps; ++m_idx) {
            const auto& base_entry = v06_recipes[m_idx];
            const auto map_path = resolveMapPath(args.datasets_root, base_entry.recipe);
            const auto loaded = hbrick::loadMovingAiMap(map_path);
            if (!loaded.ok()) {
                return 1;
            }
            const hbrick::MazeLayout layout = loaded.map.toMazeLayout(base_entry.recipe.policy);
            const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
            const auto u_span = passable_map.passableGridVertices();
            const std::vector<uint32_t> universe(u_span.begin(), u_span.end());
            const uint32_t n_uni = static_cast<uint32_t>(universe.size());
            const uint32_t diam = (layout.width() > 0U ? layout.width() - 1U : 0U)
                                + (layout.height() > 0U ? layout.height() - 1U : 0U);

            auto rangeClass = [&](const uint32_t ga, const uint32_t gb) noexcept -> uint32_t {
                if (diam == 0U) {
                    return 0U;
                }
                const hbrick::GridCoord ca = layout.coordFromVertex(hbrick::VertexId{ga});
                const hbrick::GridCoord cb = layout.coordFromVertex(hbrick::VertexId{gb});
                const uint32_t dx = ca.x >= cb.x ? (ca.x - cb.x) : (cb.x - ca.x);
                const uint32_t dy = ca.y >= cb.y ? (ca.y - cb.y) : (cb.y - ca.y);
                const double sc = static_cast<double>(dx + dy) / static_cast<double>(diam);
                if (sc < 0.25) return 0U;
                if (sc < 0.60) return 1U;
                return 2U;
            };

            for (const double theta : thetas) {
                for (const double p_back : p_backs) {
                    if (args.resume && done_v06.count(keyV06(base_entry.paper_id, theta, p_back)) > 0U) {
                        continue;
                    }
                    hbrick::RandomAsymmetricParams op{};
                    op.seed = base_entry.recipe.seed;
                    op.p_bidirectional = 0.0L;
                    op.p_one_way = 1.0L;
                    op.p_edge_drop = 0.0L;
                    op.gradient_angle_degrees = theta;
                    op.p_against_gradient = static_cast<long double>(p_back);

                    const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
                        layout, hbrick::GridEdgeConversionMode::GradientFlow, op
                    );
                    const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);
                    hbrick::GraphSearchScratch scratch(csr.numVertices());

                    hbrick::HBrickConfig hb_cfg{};
                    hb_cfg.base_tile_size = hbrick::TileSize{args.b, args.b};
                    hb_cfg.group_size = hbrick::GroupSize{args.g, args.g};
                    hb_cfg.max_depth = hbrick::kHBrickFullDepth;
                    hb_cfg.max_memory_bytes = max_mem;

                    hbrick::HBrickSkipLiftBaseline hb;
                    const auto t_hb0 = Clock::now();
                    hb.preprocess(grid_graph, layout, hb_cfg);
                    const double hb_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_hb0).count();

                    hbrick::SccDagSearchBaseline scc;
                    const auto t_scc0 = Clock::now();
                    scc.preprocess(csr, scratch);
                    const double scc_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_scc0).count();
                    const uint32_t num_sccs = scc.numComponents();
                    const double scc_ratio = csr.numVertices() > 0U
                        ? static_cast<double>(num_sccs) / static_cast<double>(csr.numVertices())
                        : 0.0;

                    hbrick::OreachBaseline oreach;
                    const auto t_or0 = Clock::now();
                    oreach.preprocess(csr, hbrick::OreachBaselineParams{}, max_mem);
                    const double or_pre_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_or0).count();

                    // Balanced pair sampling via exact index queries + short-walk harvesting on ultra-sparse graphs
                    uint32_t cap[6];
                    const uint32_t base_cap = q_count / 6U;
                    const uint32_t rem_cap = q_count % 6U;
                    for (uint32_t bi = 0U; bi < 6U; ++bi) {
                        cap[bi] = base_cap + (bi < rem_cap ? 1U : 0U);
                    }
                    struct PairPol { uint32_t sg; uint32_t dg; uint8_t pol; };
                    std::vector<PairPol> buckets[6];
                    for (uint32_t bi = 0U; bi < 6U; ++bi) {
                        buckets[bi].reserve(cap[bi]);
                    }
                    auto allFull = [&]() noexcept -> bool {
                        for (uint32_t bi = 0U; bi < 6U; ++bi) {
                            if (buckets[bi].size() < cap[bi]) return false;
                        }
                        return true;
                    };

                    const uint64_t seed_mix = static_cast<uint64_t>(std::llround(theta * 10.0)) * 131ULL
                                            + static_cast<uint64_t>(std::llround(p_back * 1000.0)) * 977ULL;
                    SplitMix64Rng rng(args.pair_seed ^ seed_mix);
                    constexpr uint32_t kDensityProbes = 16384U;
                    const uint32_t max_probes = args.quick ? 8192U : 65536U;
                    uint64_t dens_hits = 0ULL;
                    uint64_t dens_total = 0ULL;

                    for (uint32_t p = 0U; p < max_probes; ++p) {
                        if (p >= kDensityProbes && allFull()) {
                            break;
                        }
                        const uint32_t sg = universe[rng.uniform(n_uni)];
                        const uint32_t dg = universe[rng.uniform(n_uni)];
                        const uint8_t pol = hb.query(sg, dg) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        if (p < kDensityProbes && sg != dg) {
                            ++dens_total;
                            dens_hits += pol;
                        }
                        const uint32_t rc = rangeClass(sg, dg);
                        const uint32_t b_idx = rc * 2U + (pol == 1U ? 0U : 1U);
                        if (buckets[b_idx].size() < cap[b_idx]) {
                            buckets[b_idx].push_back(PairPol{sg, dg, pol});
                        }
                    }
                    const double emp_reach_frac = dens_total > 0ULL
                        ? static_cast<double>(dens_hits) / static_cast<double>(dens_total)
                        : 0.0;

                    const size_t reach_have = buckets[0].size() + buckets[2].size() + buckets[4].size();
                    const size_t reach_want = static_cast<size_t>(cap[0] + cap[2] + cap[4]);
                    if (reach_have < reach_want / 2U && csr.numVertices() > 1U) {
                        const uint32_t walk_trials = args.quick ? 512U : 4096U;
                        for (uint32_t wt = 0U; wt < walk_trials; ++wt) {
                            const uint32_t sg = universe[rng.uniform(n_uni)];
                            uint32_t cur_c = passable_map.toCompact(sg);
                            const uint32_t steps = 1U + rng.uniform(32U);
                            for (uint32_t st = 0U; st < steps; ++st) {
                                const auto nbrs = csr.outNeighbors(cur_c);
                                if (nbrs.empty()) break;
                                cur_c = nbrs[rng.uniform(static_cast<uint32_t>(nbrs.size()))];
                            }
                            const uint32_t dg = universe[cur_c];
                            if (dg == sg) continue;
                            if (hb.query(sg, dg) == hbrick::ReachabilityAnswer::Reachable) {
                                const uint32_t b_idx = rangeClass(sg, dg) * 2U;
                                if (buckets[b_idx].size() < cap[b_idx]) {
                                    buckets[b_idx].push_back(PairPol{sg, dg, 1U});
                                }
                            }
                        }
                    }

                    std::vector<PairPol> combined;
                    combined.reserve(q_count);
                    for (uint32_t bi = 0U; bi < 6U; ++bi) {
                        combined.insert(combined.end(), buckets[bi].begin(), buckets[bi].end());
                    }
                    while (combined.size() < q_count) {
                        const uint32_t sg = universe[rng.uniform(n_uni)];
                        const uint32_t dg = universe[rng.uniform(n_uni)];
                        const uint8_t pol = hb.query(sg, dg) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        combined.push_back(PairPol{sg, dg, pol});
                    }
                    for (size_t idx = combined.size(); idx > 1U; --idx) {
                        const size_t j = rng.uniform(static_cast<uint32_t>(idx));
                        std::swap(combined[idx - 1U], combined[j]);
                    }

                    std::vector<uint32_t> src_g(q_count), dst_g(q_count), src_c(q_count), dst_c(q_count);
                    std::vector<uint32_t> all_idx(q_count), reach_idx, unreach_idx;
                    std::iota(all_idx.begin(), all_idx.end(), 0U);
                    for (uint32_t idx = 0U; idx < q_count; ++idx) {
                        src_g[idx] = combined[idx].sg;
                        dst_g[idx] = combined[idx].dg;
                        src_c[idx] = passable_map.toCompact(combined[idx].sg);
                        dst_c[idx] = passable_map.toCompact(combined[idx].dg);
                        if (combined[idx].pol != 0U) reach_idx.push_back(idx);
                        else unreach_idx.push_back(idx);
                    }

                    for (uint32_t idx = 0U; idx < std::min(args.warmup_queries, q_count); ++idx) {
                        (void)scc.query(src_c[idx], dst_c[idx], scratch);
                    }
                    std::vector<uint8_t> scc_ans;
                    const DistStats scc_all = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return scc.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, all_idx, &scc_ans
                    );
                    const DistStats scc_r = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return scc.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, reach_idx
                    );
                    const DistStats scc_u = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return scc.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, unreach_idx
                    );

                    for (uint32_t idx = 0U; idx < std::min(args.warmup_queries, q_count); ++idx) {
                        (void)oreach.query(src_c[idx], dst_c[idx], scratch);
                    }
                    std::vector<uint8_t> or_ans;
                    const DistStats or_all = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return oreach.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, all_idx, &or_ans
                    );
                    const DistStats or_r = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return oreach.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, reach_idx
                    );
                    const DistStats or_u = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return oreach.query(src_c[idx], dst_c[idx], scratch) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, unreach_idx
                    );

                    for (uint32_t idx = 0U; idx < std::min(args.warmup_queries, q_count); ++idx) {
                        (void)hb.query(src_g[idx], dst_g[idx]);
                    }
                    std::vector<uint8_t> hb_ans;
                    const DistStats hb_all = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return hb.query(src_g[idx], dst_g[idx]) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, all_idx, &hb_ans
                    );
                    const DistStats hb_r = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return hb.query(src_g[idx], dst_g[idx]) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, reach_idx
                    );
                    const DistStats hb_u = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return hb.query(src_g[idx], dst_g[idx]) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        }, unreach_idx
                    );

                    const uint32_t mis = countMismatches(or_ans, scc_ans) + countMismatches(hb_ans, scc_ans);
                    if (mis > 0U) {
                        std::cerr << "FATAL: V06 sensitivity oracle mismatch!\n";
                        return 2;
                    }

                    const double sp_all = hb_all.mean > 0.0 ? or_all.mean / hb_all.mean : 0.0;
                    const double sp_r = hb_r.mean > 0.0 ? or_r.mean / hb_r.mean : 0.0;
                    const double sp_u = hb_u.mean > 0.0 ? or_u.mean / hb_u.mean : 0.0;

                    v06_csv << std::fixed << std::setprecision(4)
                            << base_entry.paper_id << "," << base_entry.recipe.map_name << ","
                            << theta << "," << p_back << ","
                            << csr.numVertices() << "," << csr.numEdges() << ","
                            << num_sccs << "," << scc_ratio << ","
                            << emp_reach_frac << "," << reach_idx.size() << "," << unreach_idx.size() << ","
                            << scc_pre_ms << "," << scc_all.mean << "," << scc_r.mean << "," << scc_u.mean << ","
                            << or_pre_ms << "," << toMiB(oreach.indexStorageBytes()) << ","
                            << or_all.mean << "," << or_r.mean << "," << or_u.mean << ","
                            << hb_pre_ms << "," << toMiB(hb.indexStorageBytes()) << ","
                            << hb_all.mean << "," << hb_r.mean << "," << hb_u.mean << ","
                            << sp_all << "," << sp_r << "," << sp_u << "," << mis << "\n";
                    v06_csv.flush();
                }
            }
            std::cout << "  [v06_sensitivity] Completed " << base_entry.paper_id << " (" << base_entry.recipe.map_name << ")\n";
        }
    }
    return 0;
}

// ============================================================================
// STAGE 7: Closure Kernel Characterization (Sections 6.1 & 7.1)
// ============================================================================
[[nodiscard]] hbrick::BitMatrix generateDensityProfileRelation(
    const uint32_t d,
    const int profile_id,
    const uint64_t seed
) {
    hbrick::BitMatrix rel(d, d);
    for (uint32_t idx = 0U; idx < d; ++idx) {
        rel.set(idx, idx);
    }
    if (d <= 1U) {
        return rel;
    }

    std::mt19937_64 rng(
        seed ^ (static_cast<uint64_t>(d) * 0x9E3779B97F4A7C15ULL) ^ static_cast<uint64_t>(profile_id * 97)
    );
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    if (profile_id == 4) {
        for (uint32_t idx = 0U; idx + 1U < d; ++idx) {
            if (u01(rng) < 0.82) {
                rel.set(idx, idx + 1U);
            }
            if (u01(rng) < 0.15) {
                rel.set(idx + 1U, idx);
            }
        }
        return rel;
    }

    const double obs_prob = (profile_id == 3) ? 0.35 : 0.0;
    uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(d) / (1.0 - obs_prob)))) + 2U;

    std::vector<int32_t> grid_to_v;
    uint32_t count = 0U;
    while (count < d) {
        grid_to_v.assign(static_cast<size_t>(side) * side, -1);
        count = 0U;
        for (uint32_t y = 0U; y < side && count < d; ++y) {
            for (uint32_t x = 0U; x < side && count < d; ++x) {
                if (obs_prob > 0.0 && u01(rng) < obs_prob) {
                    continue;
                }
                grid_to_v[static_cast<size_t>(y) * side + x] = static_cast<int32_t>(count++);
            }
        }
        if (count < d) {
            side += 4U;
        }
    }

    for (uint32_t y = 0U; y < side; ++y) {
        for (uint32_t x = 0U; x < side; ++x) {
            const int32_t u = grid_to_v[static_cast<size_t>(y) * side + x];
            if (u < 0) {
                continue;
            }
            const int dirs[2][2] = {{1, 0}, {0, 1}};
            for (int dir = 0; dir < 2; ++dir) {
                const uint32_t nx = x + static_cast<uint32_t>(dirs[dir][0]);
                const uint32_t ny = y + static_cast<uint32_t>(dirs[dir][1]);
                if (nx >= side || ny >= side) {
                    continue;
                }
                const int32_t v = grid_to_v[static_cast<size_t>(ny) * side + nx];
                if (v < 0) {
                    continue;
                }
                if (profile_id == 2) {
                    if (u01(rng) < 0.05) {
                        rel.set(static_cast<uint32_t>(v), static_cast<uint32_t>(u));
                    } else {
                        rel.set(static_cast<uint32_t>(u), static_cast<uint32_t>(v));
                    }
                } else {
                    const double p_bi = (profile_id == 0) ? 0.45 : ((profile_id == 1) ? 0.10 : 0.40);
                    if (u01(rng) < p_bi) {
                        rel.set(static_cast<uint32_t>(u), static_cast<uint32_t>(v));
                        rel.set(static_cast<uint32_t>(v), static_cast<uint32_t>(u));
                    } else if (u01(rng) < 0.5) {
                        rel.set(static_cast<uint32_t>(u), static_cast<uint32_t>(v));
                    } else {
                        rel.set(static_cast<uint32_t>(v), static_cast<uint32_t>(u));
                    }
                }
            }
        }
    }
    return rel;
}

[[nodiscard]] double matrixDensityFraction(const hbrick::BitMatrix& m) noexcept {
    const uint32_t r = m.numRows();
    const uint32_t c = m.numCols();
    if (r == 0U || c == 0U) {
        return 0.0;
    }
    const size_t full_words = static_cast<size_t>(c) / 64U;
    const uint32_t tail_bits = c % 64U;
    const uint64_t tail_mask = tail_bits == 0U ? ~0ULL : ((1ULL << tail_bits) - 1ULL);
    uint64_t ones = 0ULL;
    for (uint32_t row_idx = 0U; row_idx < r; ++row_idx) {
        const auto& row = m.row(row_idx);
        for (size_t w = 0U; w < full_words; ++w) {
            ones += static_cast<uint64_t>(std::popcount(row.word(w)));
        }
        if (tail_bits != 0U) {
            ones += static_cast<uint64_t>(std::popcount(row.word(full_words) & tail_mask));
        }
    }
    return static_cast<double>(ones) / (static_cast<double>(r) * static_cast<double>(c));
}

int runClosureKernels(const BenchArgs& args, const std::vector<ActiveRecipeEntry>& recipes) {
    std::filesystem::create_directories(args.out_dir);
    const std::filesystem::path mat_path = args.out_dir / "closure_kernel_matrix.csv";
    const std::filesystem::path tile_path = args.out_dir / "closure_tile_ablation.csv";
    const uint64_t max_mem = computeMemoryCapBytes(args.memory_gib);

    // 7A: Isolated Warshall vs M4RM (run only on shard 0 to avoid duplicate rows across shards)
    if (args.shard_index == 0U) {
        std::unordered_set<std::string> done_mat;
        if (args.resume) {
            readExistingCsvRows(mat_path, [&](const std::vector<std::string>& f) {
                if (f.size() > 2U) {
                    done_mat.insert(f[0] + "|" + f[2]);
                }
            });
        }
        const bool write_header = !args.resume || csvNeedsHeader(mat_path);
        std::ofstream csv(mat_path, args.resume ? std::ios::app : std::ios::trunc);
        if (write_header) {
            csv << "dimension,matrix_kib,profile,init_density,closure_density,squaring_steps,"
                << "warshall_median_us,warshall_q1_us,warshall_q3_us,"
                << "m4rm_median_us,m4rm_q1_us,m4rm_q3_us,"
                << "warshall_over_m4rm,faster_kernel,equal_verified\n";
        }

        const std::vector<uint32_t> dims = args.quick
            ? std::vector<uint32_t>{128U, 512U}
            : std::vector<uint32_t>{64U, 128U, 256U, 384U, 512U, 768U, 1024U, 1536U, 2048U};
        const char* const profile_names[5] = {
            "Open-V01-Dense",
            "Open-V04-Critical",
            "Open-V06-Gradient",
            "Obstacles35-V03",
            "Sparse-Corridor"
        };
        const uint32_t repeats = args.quick ? 3U : 11U;

        hbrick::KleeneSquaringOptions m4rm_opts{};
        m4rm_opts.algorithm = hbrick::ClosureAlgorithm::M4RM4Table;
        m4rm_opts.use_parallel = false;
        m4rm_opts.num_threads = 1U;

        std::cout << "[closure_kernels] Running isolated Warshall vs. M4RM kernel matrix...\n";
        for (const uint32_t d : dims) {
            const uint32_t sq_bound = hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(d);
            const double kib = (static_cast<double>(d) * static_cast<double>((d + 63U) / 64U) * 8.0) / 1024.0;
            for (int prof = 0; prof < 5; ++prof) {
                if (args.resume && done_mat.count(std::to_string(d) + "|" + profile_names[prof]) > 0U) {
                    continue;
                }
                const hbrick::BitMatrix base_rel = generateDensityProfileRelation(d, prof, 0xC0FFEE1234ULL);
                const double init_dens = matrixDensityFraction(base_rel);
                hbrick::BitMatrix w_mat = base_rel;
                hbrick::BitMatrix m_mat = base_rel;
                hbrick::BitMatrix scratch(d, d);
                uint32_t exec_sq = 0U;

                hbrick::BooleanClosure::transitiveClosureWarshallInPlace(w_mat);
                hbrick::BooleanClosure::transitiveClosureM4RMSquaringInPlace(
                    m_mat, sq_bound, &scratch, 4U, m4rm_opts, &exec_sq, nullptr
                );
                const bool eq = hbrick::bitMatricesEqual(w_mat, m_mat);
                if (!eq) {
                    std::cerr << "FATAL: Warshall vs M4RM closure mismatch at d=" << d << "\n";
                    return 2;
                }
                const double cl_dens = matrixDensityFraction(w_mat);

                std::vector<double> w_us;
                std::vector<double> m_us;
                w_us.reserve(repeats);
                m_us.reserve(repeats);
                for (uint32_t rep = 0U; rep < repeats; ++rep) {
                    if ((rep % 2U) == 0U) {
                        w_mat = base_rel;
                        const auto t0 = Clock::now();
                        hbrick::BooleanClosure::transitiveClosureWarshallInPlace(w_mat);
                        w_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());

                        m_mat = base_rel;
                        const auto t2 = Clock::now();
                        hbrick::BooleanClosure::transitiveClosureM4RMSquaringInPlace(
                            m_mat, sq_bound, &scratch, 4U, m4rm_opts, &exec_sq, nullptr
                        );
                        m_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t2).count());
                    } else {
                        m_mat = base_rel;
                        const auto t2 = Clock::now();
                        hbrick::BooleanClosure::transitiveClosureM4RMSquaringInPlace(
                            m_mat, sq_bound, &scratch, 4U, m4rm_opts, &exec_sq, nullptr
                        );
                        m_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t2).count());

                        w_mat = base_rel;
                        const auto t0 = Clock::now();
                        hbrick::BooleanClosure::transitiveClosureWarshallInPlace(w_mat);
                        w_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
                    }
                }
                const DistStats wst = computeDistStats(w_us);
                const DistStats mst = computeDistStats(m_us);
                const double ratio = mst.median > 0.0 ? (wst.median / mst.median) : 0.0;

                csv << std::fixed << std::setprecision(4)
                    << d << "," << kib << "," << profile_names[prof] << ","
                    << init_dens << "," << cl_dens << "," << exec_sq << ","
                    << wst.median << "," << wst.q1 << "," << wst.q3 << ","
                    << mst.median << "," << mst.q1 << "," << mst.q3 << ","
                    << ratio << "," << (wst.median <= mst.median ? "Warshall" : "M4RM") << ","
                    << (eq ? 1 : 0) << "\n";
                csv.flush();
            }
        }
    }

    // 7B: End-to-end tile design vs closure kernel selection
    {
        std::unordered_set<std::string> done_tile;
        if (args.resume) {
            readExistingCsvRows(tile_path, [&](const std::vector<std::string>& f) {
                if (f.size() > 3U) {
                    done_tile.insert(f[0] + "|" + f[2] + "|" + f[3]);
                }
            });
        }
        const bool write_header = !args.resume || csvNeedsHeader(tile_path);
        std::ofstream csv(tile_path, args.resume ? std::ios::app : std::ios::trunc);
        if (write_header) {
            csv << "paper_id,map_name,regime,b,g,num_base_tiles,base_d_le_512,base_d_gt_512,mean_base_nT,max_base_nT,"
                << "super_d_le_512,super_d_gt_512,max_gamma_U,"
                << "warshall_pre_ms,m4rm_pre_ms,hybrid_pre_ms,"
                << "warshall_index_bytes,m4rm_index_bytes,hybrid_index_bytes,"
                << "warshall_query_us,m4rm_query_us,hybrid_query_us,byte_identical\n";
        }

        const std::vector<uint32_t> b_vals = args.quick
            ? std::vector<uint32_t>{16U, 24U}
            : std::vector<uint32_t>{16U, 24U, 32U, 48U};
        const uint32_t q_count = args.quick ? 512U : 4096U;

        std::cout << "[closure_kernels] Running end-to-end tile design vs. kernel ablation...\n";
        size_t executed = 0U;
        for (const auto& entry : recipes) {
            if (entry.regime != "V01" && entry.regime != "V04" && entry.regime != "V06") {
                continue;
            }
            if (args.quick && executed >= 2U) {
                break;
            }
            ++executed;

            bool all_b_done = args.resume;
            if (all_b_done) {
                for (const uint32_t bv : b_vals) {
                    if (done_tile.count(entry.paper_id + "|" + entry.regime + "|" + std::to_string(bv)) == 0U) {
                        all_b_done = false;
                        break;
                    }
                }
            }
            if (all_b_done) {
                continue;
            }

            const auto map_path = resolveMapPath(args.datasets_root, entry.recipe);
            const auto loaded = hbrick::loadMovingAiMap(map_path);
            if (!loaded.ok()) {
                return 1;
            }
            const hbrick::MazeLayout layout = loaded.map.toMazeLayout(entry.recipe.policy);
            const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
                layout, entry.recipe.mode, orientationFromRecipe(entry.recipe)
            );
            const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
            const auto u_span = passable_map.passableGridVertices();
            const std::vector<uint32_t> universe(u_span.begin(), u_span.end());

            SplitMix64Rng qrng(args.pair_seed ^ entry.recipe.seed);
            std::vector<uint32_t> src_g(q_count), dst_g(q_count);
            for (uint32_t idx = 0U; idx < q_count; ++idx) {
                src_g[idx] = universe[qrng.uniform(static_cast<uint32_t>(universe.size()))];
                dst_g[idx] = universe[qrng.uniform(static_cast<uint32_t>(universe.size()))];
            }
            std::vector<uint32_t> all_idx(q_count);
            std::iota(all_idx.begin(), all_idx.end(), 0U);

            for (const uint32_t b_val : b_vals) {
                if (args.resume && done_tile.count(entry.paper_id + "|" + entry.regime + "|" + std::to_string(b_val)) > 0U) {
                    continue;
                }
                hbrick::HBrickConfig cfg{};
                cfg.base_tile_size = hbrick::TileSize{b_val, b_val};
                cfg.group_size = hbrick::GroupSize{4U, 4U};
                cfg.max_depth = hbrick::kHBrickFullDepth;
                cfg.max_memory_bytes = max_mem;

                const char* const algs[3] = {"warshall", "m4rm", "hybrid"};
                double pre_ms[3] = {0.0, 0.0, 0.0};
                uint64_t idx_bytes[3] = {0ULL, 0ULL, 0ULL};
                double q_us[3] = {0.0, 0.0, 0.0};
                uint32_t num_tiles = 0U;
                uint32_t d_le_512 = 0U;
                uint32_t d_gt_512 = 0U;
                double mean_nt = 0.0;
                uint32_t max_nt = 0U;
                uint32_t super_le_512 = 0U;
                uint32_t super_gt_512 = 0U;
                uint32_t max_gu = 0U;

                for (int step = 0; step < 3; ++step) {
                    const int k_idx = (step + static_cast<int>(b_val)) % 3;
                    ::setenv("HBRICK_CLOSURE_ALGORITHM", algs[k_idx], 1);

                    hbrick::HBrickSkipLiftBaseline hb;
                    const auto t0 = Clock::now();
                    hb.preprocess(grid_graph, layout, cfg);
                    const auto t1 = Clock::now();
                    pre_ms[k_idx] = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    idx_bytes[k_idx] = hb.indexStorageBytes();

                    if (k_idx == 2) {
                        const auto summaries = hb.index().brickIndex().tiles().summaries();
                        uint64_t sum_nt = 0ULL;
                        for (const auto& ts : summaries) {
                            const uint32_t nt = ts.numLocalVertices();
                            if (nt == 0U) {
                                continue;
                            }
                            ++num_tiles;
                            sum_nt += nt;
                            max_nt = std::max(max_nt, nt);
                            if (nt <= 512U) ++d_le_512;
                            else ++d_gt_512;
                        }
                        mean_nt = num_tiles > 0U ? static_cast<double>(sum_nt) / static_cast<double>(num_tiles) : 0.0;

                        for (uint32_t lv = 1U; hb.index().hasSuperLevel(lv); ++lv) {
                            for (const auto& st : hb.index().superLevel(lv)) {
                                const uint32_t gu = static_cast<uint32_t>(st.gamma.ports.size());
                                if (gu == 0U) {
                                    continue;
                                }
                                max_gu = std::max(max_gu, gu);
                                if (gu <= 512U) ++super_le_512;
                                else ++super_gt_512;
                            }
                        }
                    }

                    for (uint32_t w = 0U; w < 128U && w < q_count; ++w) {
                        (void)hb.query(src_g[w], dst_g[w]);
                    }
                    const DistStats qst = measureChunkedSubset(
                        [&](const uint32_t idx) -> uint8_t {
                            return hb.query(src_g[idx], dst_g[idx]) == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                        },
                        all_idx
                    );
                    q_us[k_idx] = qst.mean;
                }
                ::unsetenv("HBRICK_CLOSURE_ALGORITHM");

                const bool byte_eq = (idx_bytes[0] == idx_bytes[1]) && (idx_bytes[1] == idx_bytes[2]);
                csv << std::fixed << std::setprecision(4)
                    << entry.paper_id << "," << entry.recipe.map_name << "," << entry.regime << ","
                    << b_val << ",4," << num_tiles << "," << d_le_512 << "," << d_gt_512 << ","
                    << mean_nt << "," << max_nt << ","
                    << super_le_512 << "," << super_gt_512 << "," << max_gu << ","
                    << pre_ms[0] << "," << pre_ms[1] << "," << pre_ms[2] << ","
                    << idx_bytes[0] << "," << idx_bytes[1] << "," << idx_bytes[2] << ","
                    << q_us[0] << "," << q_us[1] << "," << q_us[2] << ","
                    << (byte_eq ? 1 : 0) << "\n";
                csv.flush();
            }
        }
    }
    return 0;
}

void printHelp(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "  --stage <list>          Comma-separated: all | graph_stats | param_sweep |\n"
        << "                          threeway | external | batch_multisample |\n"
        << "                          polarity_and_v06 | closure_kernels (default: all)\n"
        << "  --recipes-dir <path>    Directory of orientation recipes (default: recipes)\n"
        << "  --datasets-root <path>  Moving AI dataset root (default: datasets/movingai)\n"
        << "  --out-dir <path>        Output directory (default: campaigns/manuscript)\n"
        << "  --b <size>              Default base tile side (default: 24)\n"
        << "  --g <size>              Default aggregation factor (default: 4)\n"
        << "  --query-count <N>       Single-pair timed queries (default: 8192)\n"
        << "  --warmup-queries <N>    Single-pair warmup queries (default: 128)\n"
        << "  --pair-seed <S>         Seed for balanced pair sampling (default: 0xBEEFCAFE)\n"
        << "  --k <list>              Batch sizes for batch_multisample (default: 4,16,64)\n"
        << "  --batch-samples <N>     Independent endpoint draws per cell (default: 10)\n"
        << "  --batch-repeats <N>     Timed repetitions per draw (default: 11)\n"
        << "  --batch-warmup <N>      Warmup passes per draw (default: 2)\n"
        << "  --memory-gib <G>        Retained index memory limit in GiB (default: 2.5)\n"
        << "  --timeout-sec <S>       2-hop preprocessing timeout in seconds (default: 300)\n"
        << "  --shard-index <I>       Zero-based shard index (default: 0)\n"
        << "  --shard-count <N>       Number of worker shards (default: 1)\n"
        << "  --resume                Skip rows already recorded in existing CSVs\n"
        << "  --quick                 Run fast smoke-test subset across selected stages\n"
        << "  --help                  Show this help message\n";
}

bool parseArgs(const int argc, char** argv, BenchArgs& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            printHelp(argv[0]);
            std::exit(0);
        } else if (a == "--stage" && i + 1 < argc) {
            args.stage = argv[++i];
        } else if (a == "--recipes-dir" && i + 1 < argc) {
            args.recipes_dir = argv[++i];
        } else if (a == "--datasets-root" && i + 1 < argc) {
            args.datasets_root = argv[++i];
        } else if (a == "--out-dir" && i + 1 < argc) {
            args.out_dir = argv[++i];
        } else if (a == "--b" && i + 1 < argc) {
            args.b = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--g" && i + 1 < argc) {
            args.g = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--query-count" && i + 1 < argc) {
            args.query_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--warmup-queries" && i + 1 < argc) {
            args.warmup_queries = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--pair-seed" && i + 1 < argc) {
            args.pair_seed = std::stoull(argv[++i], nullptr, 0);
        } else if (a == "--k" && i + 1 < argc) {
            args.k_values.clear();
            std::stringstream ss(argv[++i]);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (!tok.empty()) {
                    args.k_values.push_back(static_cast<uint32_t>(std::stoul(tok)));
                }
            }
        } else if (a == "--batch-samples" && i + 1 < argc) {
            args.batch_samples = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--batch-repeats" && i + 1 < argc) {
            args.batch_repeats = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--batch-warmup" && i + 1 < argc) {
            args.batch_warmup = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--memory-gib" && i + 1 < argc) {
            args.memory_gib = std::stod(argv[++i]);
        } else if (a == "--timeout-sec" && i + 1 < argc) {
            args.timeout_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--shard-index" && i + 1 < argc) {
            args.shard_index = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--shard-count" && i + 1 < argc) {
            args.shard_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--resume") {
            args.resume = true;
        } else if (a == "--quick") {
            args.quick = true;
        } else {
            std::cerr << "Unknown or incomplete argument: " << a << "\n";
            return false;
        }
    }
    if (args.shard_count == 0U || args.shard_index >= args.shard_count) {
        std::cerr << "Invalid shard specification: index=" << args.shard_index
                  << " count=" << args.shard_count << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    raiseStackLimitIfNeeded();
    BenchArgs args;
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    std::unordered_set<std::string> requested;
    {
        std::stringstream ss(args.stage);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) {
                requested.insert(tok);
            }
        }
    }
    const auto want = [&](const char* name) {
        return requested.count("all") > 0U || requested.count(name) > 0U;
    };

    const std::vector<ActiveRecipeEntry> all_recipes = loadActive48Recipes(args.recipes_dir);
    if (all_recipes.empty()) {
        std::cerr << "Error: no active M1..M8 recipes found in " << args.recipes_dir << "\n";
        return 1;
    }
    const std::vector<ActiveRecipeEntry> recipes = selectShard(
        all_recipes, args.shard_index, args.shard_count
    );
    std::cout << "Loaded " << recipes.size() << " active recipes (of " << all_recipes.size()
              << ", shard " << args.shard_index << "/" << args.shard_count << ").\n";

    if (want("graph_stats") && runGraphStats(args, recipes) != 0) return 1;
    if (want("param_sweep") && runParamSweep(args, recipes) != 0) return 1;
    if (want("threeway") && runThreeway(args, recipes) != 0) return 1;
    if (want("external") && runExternal(args, recipes) != 0) return 1;
    if (want("batch_multisample") && runBatchMultisample(args, recipes) != 0) return 1;
    if (want("polarity_and_v06") && runPolarityAndV06(args, recipes) != 0) return 1;
    if (want("closure_kernels") && runClosureKernels(args, recipes) != 0) return 1;

    std::cout << "Completed manuscript benchmark stages in " << args.out_dir << "\n";
    return 0;
}
