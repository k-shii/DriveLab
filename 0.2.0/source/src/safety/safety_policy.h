#pragma once

#include "core/drive.h"
#include "core/execution_mode.h"
#include "core/operation.h"
#include "core/result.h"

namespace drivelab {

struct ExecutionAuthorization {
    bool may_execute = false;
    bool simulated = false;
    bool planned_only = false;
};

class SafetyPolicy {
public:
    [[nodiscard]] Result<ExecutionAuthorization> authorize(
        const Drive& drive,
        SafetyClass safety_class,
        ExecutionMode mode) const;
};

}  // namespace drivelab
