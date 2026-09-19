#include "report/report_writer.h"

#include <utility>

namespace drivelab {

DisabledReportWriter::DisabledReportWriter(std::string reason)
    : reason_(std::move(reason)) {}

Result<std::filesystem::path> DisabledReportWriter::write(const Report&) {
    return Result<std::filesystem::path>::failure({
        ErrorCode::ExecutionDisabled,
        "DisabledReportWriter",
        reason_
    });
}

Result<std::filesystem::path> MemoryReportWriter::write(const Report& report) {
    reports_.push_back(report);
    return Result<std::filesystem::path>::success(
        std::filesystem::path("memory://report/") /
        std::to_string(reports_.size()));
}

const std::vector<Report>& MemoryReportWriter::reports() const {
    return reports_;
}

}  // namespace drivelab
