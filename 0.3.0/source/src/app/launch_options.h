#pragma once

#include "core/execution_mode.h"
#include "core/result.h"

#include <string>
#include <vector>

namespace drivelab {

struct LaunchOptions {
    ExecutionMode mode = ExecutionMode::Real;
    bool show_help = false;
    bool show_version = false;
};

Result<LaunchOptions> parseLaunchOptions(const std::vector<std::string>& arguments);
std::string usage(std::string executable_name);

}  // namespace drivelab
