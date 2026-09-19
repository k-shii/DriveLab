#pragma once

#include "process/process_runner.h"

namespace drivelab {

class PosixProcessRunner final : public ProcessRunner {
public:
    Result<ProcessResult> run(const ProcessSpec& spec) override;
};

}  // namespace drivelab
