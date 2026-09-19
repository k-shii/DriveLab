#pragma once

#include <string_view>

namespace drivelab {

enum class ExecutionMode {
    Real,
    Demo,
    DryRun
};

constexpr std::string_view executionModeName(ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::Real: return "real";
        case ExecutionMode::Demo: return "demo";
        case ExecutionMode::DryRun: return "dry-run";
    }
    return "unknown";
}

}  // namespace drivelab
