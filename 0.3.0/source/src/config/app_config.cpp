#include "config/app_config.h"

namespace drivelab {

Result<void> AppConfig::validate() const {
    if (max_parallel_drives == 0) {
        return Result<void>::failure({
            ErrorCode::InvalidArgument,
            "AppConfig",
            "max_parallel_drives must be greater than zero"
        });
    }
    if (report_directory.empty()) {
        return Result<void>::failure({
            ErrorCode::InvalidArgument,
            "AppConfig",
            "report_directory must not be empty"
        });
    }
    return Result<void>::success();
}

}  // namespace drivelab
