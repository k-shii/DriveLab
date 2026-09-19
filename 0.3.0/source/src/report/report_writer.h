#pragma once

#include "core/drive.h"
#include "core/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace drivelab {

inline constexpr int kReportSchemaVersion = 1;

struct Report {
    int schema_version = kReportSchemaVersion;
    DriveIdentity drive_identity;
    std::string title;
    std::string body;
};

class ReportWriter {
public:
    virtual ~ReportWriter() = default;
    virtual Result<std::filesystem::path> write(const Report& report) = 0;
};

class DisabledReportWriter final : public ReportWriter {
public:
    explicit DisabledReportWriter(std::string reason);
    Result<std::filesystem::path> write(const Report& report) override;

private:
    std::string reason_;
};

class MemoryReportWriter final : public ReportWriter {
public:
    Result<std::filesystem::path> write(const Report& report) override;
    [[nodiscard]] const std::vector<Report>& reports() const;

private:
    std::vector<Report> reports_;
};

}  // namespace drivelab
