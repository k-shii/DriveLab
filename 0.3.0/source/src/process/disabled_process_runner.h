#pragma once

#include "core/execution_mode.h"
#include "process/process_runner.h"

#include <vector>

namespace drivelab {

class DisabledProcessRunner final : public ProcessRunner {
public:
    explicit DisabledProcessRunner(ExecutionMode mode);

    Result<ProcessResult> run(const ProcessSpec& spec) override;
    [[nodiscard]] const std::vector<ProcessSpec>& planned() const;

private:
    ExecutionMode mode_;
    std::vector<ProcessSpec> planned_;
};

}  // namespace drivelab
