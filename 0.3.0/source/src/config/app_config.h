#pragma once

#include "core/execution_mode.h"
#include "core/result.h"

#include <cstddef>
#include <filesystem>

namespace drivelab {

inline constexpr int kConfigSchemaVersion = 1;

struct AppConfig {
    ExecutionMode mode = ExecutionMode::Real;
    std::size_t max_parallel_drives = 2;
    std::filesystem::path report_directory = "/var/lib/drivelab/reports";
    bool mouse_enabled = true;

    [[nodiscard]] Result<void> validate() const;
};

}  // namespace drivelab
