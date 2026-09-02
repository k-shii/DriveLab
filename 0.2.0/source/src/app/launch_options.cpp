#include "app/launch_options.h"

#include <sstream>
#include <utility>

namespace drivelab {

Result<LaunchOptions> parseLaunchOptions(const std::vector<std::string>& arguments) {
    LaunchOptions options;
    bool mode_selected = false;
    for (const std::string& argument : arguments) {
        if (argument == "--demo" || argument == "--dry-run") {
            if (mode_selected) {
                return Result<LaunchOptions>::failure({
                    ErrorCode::InvalidArgument,
                    "CommandLine",
                    "--demo and --dry-run are mutually exclusive"
                });
            }
            options.mode = argument == "--demo" ? ExecutionMode::Demo : ExecutionMode::DryRun;
            mode_selected = true;
        } else if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--version") {
            options.show_version = true;
        } else {
            return Result<LaunchOptions>::failure({
                ErrorCode::InvalidArgument,
                "CommandLine",
                "Unknown option: " + argument
            });
        }
    }
    return Result<LaunchOptions>::success(options);
}

std::string usage(std::string executable_name) {
    std::ostringstream output;
    output << "Usage: " << std::move(executable_name) << " [--demo | --dry-run] [--help] [--version]\n"
           << "  --demo     Run the existing no-I/O mock TUI\n"
           << "  --dry-run  Inspect the 0.2 Core plan without executing processes\n";
    return output.str();
}

}  // namespace drivelab
