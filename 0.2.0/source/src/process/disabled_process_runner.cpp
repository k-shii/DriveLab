#include "process/disabled_process_runner.h"

#include <sstream>

namespace drivelab {
namespace {

std::string quoteForDisplay(const std::string& value) {
    if (value.find_first_of(" \t\"'") == std::string::npos) return value;
    std::string output = "\"";
    for (char character : value) {
        if (character == '\\' || character == '"') output.push_back('\\');
        output.push_back(character);
    }
    output.push_back('"');
    return output;
}

}  // namespace

std::string renderProcessSpec(const ProcessSpec& spec) {
    std::ostringstream output;
    output << quoteForDisplay(spec.executable);
    for (const std::string& argument : spec.arguments) {
        output << ' ' << quoteForDisplay(argument);
    }
    return output.str();
}

DisabledProcessRunner::DisabledProcessRunner(ExecutionMode mode) : mode_(mode) {}

Result<ProcessResult> DisabledProcessRunner::run(const ProcessSpec& spec) {
    planned_.push_back(spec);
    return Result<ProcessResult>::failure({
        ErrorCode::ExecutionDisabled,
        "ProcessRunner",
        "Process execution is disabled in " + std::string(executionModeName(mode_)) +
            " mode: " + renderProcessSpec(spec)
    });
}

const std::vector<ProcessSpec>& DisabledProcessRunner::planned() const {
    return planned_;
}

}  // namespace drivelab
