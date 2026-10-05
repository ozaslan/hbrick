#include "hbrick/bench/benchmark_campaign_log.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace hbrick {

namespace {

[[nodiscard]] std::string currentUtcTimestamp() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t time_value = std::chrono::system_clock::to_time_t(now);
    std::tm utc_time{};
#if defined(_WIN32)
    gmtime_s(&utc_time, &time_value);
#else
    gmtime_r(&time_value, &utc_time);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

[[nodiscard]] std::string sanitizeForFilename(const std::string& value) {
    std::string result = value;
    for (char& character : result) {
        const bool keep = (std::isalnum(static_cast<unsigned char>(character)) != 0)
            || character == '-' || character == '_' || character == '.';
        if (!keep) {
            character = '_';
        }
    }
    return result;
}

}  // namespace

BenchmarkCampaignLogger::~BenchmarkCampaignLogger() {
    close();
}

bool BenchmarkCampaignLogger::open(
    const std::filesystem::path& logs_dir,
    const std::string& campaign_id,
    std::string& error_message
) {
    close();

    std::error_code error;
    if (!std::filesystem::create_directories(logs_dir, error) && error) {
        error_message = "Failed to create logs directory: " + error.message();
        return false;
    }

    const std::string timestamp = currentUtcTimestamp();
    log_path_ = logs_dir / (sanitizeForFilename(campaign_id) + '_' + timestamp + ".log");
    file_ = std::fopen(log_path_.string().c_str(), "a");
    if (file_ == nullptr) {
        error_message = "Failed to open log file: " + log_path_.string();
        return false;
    }

    infof("Campaign log opened for %s", campaign_id.c_str());
    return true;
}

void BenchmarkCampaignLogger::close() noexcept {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void BenchmarkCampaignLogger::writeLine(const char* level, const char* message) {
    if (file_ == nullptr) {
        return;
    }
    std::fprintf(file_, "%s [%s] %s\n", currentUtcTimestamp().c_str(), level, message);
    std::fflush(file_);
    std::fprintf(stderr, "[%s] %s\n", level, message);
}

void BenchmarkCampaignLogger::writeLineV(
    const char* level,
    const char* format,
    std::va_list arguments
) noexcept {
    if (file_ == nullptr) {
        return;
    }
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    writeLine(level, buffer);
}

void BenchmarkCampaignLogger::info(const char* message) {
    writeLine("INFO", message);
}

void BenchmarkCampaignLogger::warn(const char* message) {
    writeLine("WARN", message);
}

void BenchmarkCampaignLogger::error(const char* message) {
    writeLine("ERROR", message);
}

void BenchmarkCampaignLogger::infof(const char* format, ...) noexcept {
    std::va_list arguments;
    va_start(arguments, format);
    writeLineV("INFO", format, arguments);
    va_end(arguments);
}

void BenchmarkCampaignLogger::warnf(const char* format, ...) noexcept {
    std::va_list arguments;
    va_start(arguments, format);
    writeLineV("WARN", format, arguments);
    va_end(arguments);
}

void BenchmarkCampaignLogger::errorf(const char* format, ...) noexcept {
    std::va_list arguments;
    va_start(arguments, format);
    writeLineV("ERROR", format, arguments);
    va_end(arguments);
}

}  // namespace hbrick
