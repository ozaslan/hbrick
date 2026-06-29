/**
 * @file preprocess_memory_ledger.hpp
 * @ingroup hbrick_tile
 * @brief Tracks cumulative preprocess heap usage against a global byte cap.
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <limits>

#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

/**
 * @brief Running total of bytes charged during flat-BRICK preprocess.
 * @ingroup hbrick_tile
 *
 * Charges are applied at allocation time using exact sizes (matrix dimensions,
 * vector capacity, CSR storage). No predictive preflight or conservative estimates.
 */
class PreprocessMemoryLedger {
public:
    explicit PreprocessMemoryLedger(
        uint64_t cap_bytes = std::numeric_limits<uint64_t>::max()
    ) noexcept
        : cap_bytes_(cap_bytes) {}

    /** @brief Resets the ledger to an empty running total with a new cap. @ingroup hbrick_tile */
    void reset(uint64_t cap_bytes) noexcept {
        cap_bytes_ = cap_bytes;
        charged_bytes_ = 0U;
    }

    [[nodiscard]] bool isUnlimited() const noexcept {
        return cap_bytes_ >= kHBrickUnlimitedMemoryBytes;
    }

    [[nodiscard]] uint64_t capBytes() const noexcept { return cap_bytes_; }

    [[nodiscard]] uint64_t chargedBytes() const noexcept { return charged_bytes_; }

    /**
     * @brief Records @p bytes against the running total when they fit under the cap.
     * @ingroup hbrick_tile
     * @return @c false when the charge would exceed @ref capBytes().
     */
    [[nodiscard]] bool tryCharge(uint64_t bytes) noexcept {
        if (bytes == 0U) {
            return true;
        }
        if (!isUnlimited() && charged_bytes_ + bytes > cap_bytes_) {
            return false;
        }
        charged_bytes_ += bytes;
        return true;
    }

    /**
     * @brief Removes a prior charge when a preprocess step is rolled back.
     * @ingroup hbrick_tile
     */
    void releaseCharge(uint64_t bytes) noexcept {
        if (bytes == 0U) {
            return;
        }
        assert(bytes <= charged_bytes_);
        charged_bytes_ -= bytes;
    }

private:
    uint64_t cap_bytes_ = std::numeric_limits<uint64_t>::max();
    uint64_t charged_bytes_ = 0U;
};

}  // namespace hbrick
