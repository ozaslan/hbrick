/**
 * @file benchmark_campaign_log.hpp
 * @ingroup hbrick_bench
 * @brief Per-campaign log file writer for CLI benchmark runs.
 */

#pragma once

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>

namespace hbrick {

/**
 * @brief Appends timestamped lines to a file under @c logs/.
 * @ingroup hbrick_bench
 */
class BenchmarkCampaignLogger {
public:
    BenchmarkCampaignLogger() = default;
    ~BenchmarkCampaignLogger();

    BenchmarkCampaignLogger(const BenchmarkCampaignLogger&) = delete;
    BenchmarkCampaignLogger& operator=(const BenchmarkCampaignLogger&) = delete;

    /** @brief Opens @c logs/<campaign_id>_<timestamp>.log under @p logs_dir. @ingroup hbrick_bench */
    [[nodiscard]] bool open(
        const std::filesystem::path& logs_dir,
        const std::string& campaign_id,
        std::string& error_message
    );

    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return file_ != nullptr; }

    [[nodiscard]] const std::filesystem::path& logPath() const noexcept {
        return log_path_;
    }

    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);

    void infof(const char* format, ...) noexcept;
    void warnf(const char* format, ...) noexcept;
    void errorf(const char* format, ...) noexcept;

private:
    void writeLine(const char* level, const char* message);
    void writeLineV(const char* level, const char* format, std::va_list arguments) noexcept;

    std::FILE* file_ = nullptr;
    std::filesystem::path log_path_;
};

}  // namespace hbrick
