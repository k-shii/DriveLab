#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace drivelab {

enum class LogSeverity {
    Debug,
    Info,
    Warning,
    Error
};

struct LogRecord {
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    LogSeverity severity = LogSeverity::Info;
    std::string component;
    std::optional<std::uint64_t> job_id;
    std::string message;
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogRecord record) = 0;
};

class NullLogger final : public ILogger {
public:
    void log(LogRecord) override {}
};

class VectorLogger final : public ILogger {
public:
    void log(LogRecord record) override;
    [[nodiscard]] const std::vector<LogRecord>& records() const;

private:
    std::vector<LogRecord> records_;
};

class OstreamLogger final : public ILogger {
public:
    explicit OstreamLogger(std::ostream& output);
    void log(LogRecord record) override;

private:
    std::ostream& output_;
};

std::string logSeverityName(LogSeverity severity);

}  // namespace drivelab
