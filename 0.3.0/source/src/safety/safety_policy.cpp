#include "safety/safety_policy.h"

namespace drivelab {

Result<ExecutionAuthorization> SafetyPolicy::authorize(
    const Drive& drive,
    SafetyClass safety_class,
    ExecutionMode mode) const {
    Result<std::string> identity = drive.identity.lockKey();
    if (!identity) {
        return Result<ExecutionAuthorization>::failure(identity.error());
    }

    if (drive.isProtected() && safety_class != SafetyClass::StatusOnly) {
        return Result<ExecutionAuthorization>::failure({
            ErrorCode::SafetyBlocked,
            "SafetyPolicy",
            "Protected drives expose status only"
        });
    }

    if (drive.status == DriveStatus::Unknown && safety_class != SafetyClass::StatusOnly) {
        return Result<ExecutionAuthorization>::failure({
            ErrorCode::SafetyBlocked,
            "SafetyPolicy",
            "Unknown drive ownership defaults to status-only access"
        });
    }

    if (mode == ExecutionMode::DryRun) {
        return Result<ExecutionAuthorization>::success({false, false, true});
    }

    if (mode == ExecutionMode::Demo) {
        return Result<ExecutionAuthorization>::success({true, true, false});
    }

    if (safety_class == SafetyClass::Destructive ||
        safety_class == SafetyClass::Irreversible) {
        return Result<ExecutionAuthorization>::failure({
            ErrorCode::Unavailable,
            "SafetyPolicy",
            "Real destructive authorization is intentionally unavailable before the 0.3 safety engine"
        });
    }

    return Result<ExecutionAuthorization>::success({true, false, false});
}

}  // namespace drivelab
