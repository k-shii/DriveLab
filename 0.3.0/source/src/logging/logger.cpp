#include "logging/logger.h"

#include <ctime>
#include <iomanip>
#include <utility>

namespace drivelab {

void VectorLogger::log(LogRecord record) {
    records_.push_back(std::move(record));
}

const std::vector<LogRecord>& VectorLogger::records() const {
    return records_;
}

OstreamLogger::OstreamLogger(std::ostream& output) : output_(output) {}

void OstreamLogger::log(LogRecord record) {
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(record.timestamp);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &raw_time);
#else
    localtime_r(&raw_time, &local_time);
#endif
    output_ << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << ' '
            << std::left << std::setw(5) << logSeverityName(record.severity) << ' '
            << record.component;
    if (record.job_id) output_ << " Job#" << *record.job_id;
    output_ << "  " << record.message << '\n';
}

std::string logSeverityName(LogSeverity severity) {
    switch (severity) {
        case LogSeverity::Debug: return "DEBUG";
        case LogSeverity::Info: return "INFO";
        case LogSeverity::Warning: return "WARN";
        case LogSeverity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace drivelab
