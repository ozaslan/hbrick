#include "hbrick/tile/hbrick_build_format.hpp"

#include <cstdio>

namespace hbrick {

const char* hbrickBuildStageLabel(const HBrickBuildStage stage) noexcept {
    switch (stage) {
        case HBrickBuildStage::Idle:
            return "Idle";
        case HBrickBuildStage::BaseTiles:
            return "Base tiles";
        case HBrickBuildStage::FinalizeBrick:
            return "Port graph";
        case HBrickBuildStage::Hierarchy:
            return "Hierarchy";
        case HBrickBuildStage::SuperTiles:
            return "Super tiles";
        case HBrickBuildStage::Finished:
            return "Finished";
    }
    return "Unknown";
}

void formatHBrickBuildNanoseconds(
    char* const buffer,
    const std::size_t size,
    const uint64_t nanoseconds
) noexcept {
    const double milliseconds = static_cast<double>(nanoseconds) / 1'000'000.0;
    std::snprintf(buffer, size, "%.1f ms", milliseconds);
}

void formatHBrickBuildBytes(
    char* const buffer,
    const std::size_t size,
    const uint64_t bytes
) noexcept {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f GiB",
            static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)
        );
        return;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f MiB",
            static_cast<double>(bytes) / (1024.0 * 1024.0)
        );
        return;
    }
    if (bytes >= 1024ULL) {
        std::snprintf(
            buffer,
            size,
            "%.1f KiB",
            static_cast<double>(bytes) / 1024.0
        );
        return;
    }
    std::snprintf(buffer, size, "%llu B", static_cast<unsigned long long>(bytes));
}

void formatHBrickBuildProgressDetail(
    char* const buffer,
    const std::size_t size,
    const HBrickBuildProgress& progress
) noexcept {
    switch (progress.stage) {
        case HBrickBuildStage::BaseTiles:
            std::snprintf(
                buffer,
                size,
                "Base tile %u / %u",
                progress.current_base_tile_index + 1U,
                progress.num_base_tiles
            );
            break;
        case HBrickBuildStage::FinalizeBrick:
            std::snprintf(buffer, size, "Building port index and seam graph");
            break;
        case HBrickBuildStage::Hierarchy:
            std::snprintf(buffer, size, "Building region hierarchy");
            break;
        case HBrickBuildStage::SuperTiles:
            std::snprintf(
                buffer,
                size,
                "Super level L%u region %u",
                progress.current_level,
                progress.current_node_index + 1U
            );
            break;
        default:
            std::snprintf(buffer, size, "%s", hbrickBuildStageLabel(progress.stage));
            break;
    }
}

}  // namespace hbrick
