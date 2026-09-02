#pragma once

#include "core/result.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace drivelab {

struct ProcessSpec {
    std::string executable;
    std::vector<std::string> arguments;
    std::map<std::string, std::string> environment;
    std::chrono::milliseconds timeout{0};
    bool capture_stdout = true;
    bool capture_stderr = true;
};

struct ProcessResult {
    int exit_code = -1;
    int terminating_signal = 0;
    bool timed_out = false;
    std::string stdout_text;
    std::string stderr_text;
};

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;
    virtual Result<ProcessResult> run(const ProcessSpec& spec) = 0;
};

std::string renderProcessSpec(const ProcessSpec& spec);

}  // namespace drivelab
