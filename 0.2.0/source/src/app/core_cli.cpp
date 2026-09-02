#include "app/core_cli.h"

#include "app/core_application.h"
#include "app/launch_options.h"
#include "core/version.h"

#include <iostream>
#include <utility>

namespace drivelab {

int runDryRunBootstrap(std::ostream& output, std::ostream& error_output) {
    NullLogger logger;
    AppConfig config;
    config.mode = ExecutionMode::DryRun;
    Result<std::unique_ptr<CoreApplication>> application =
        CoreApplication::createMock(config, logger);
    if (!application) {
        error_output << application.error().component << ": "
                     << application.error().message << '\n';
        return 1;
    }

    Result<CoreSnapshot> snapshot = application.value()->snapshot();
    if (!snapshot) {
        error_output << snapshot.error().component << ": "
                     << snapshot.error().message << '\n';
        return 1;
    }

    output << "DriveLab Core " << kVersion << '\n'
           << "Mode: dry-run\n"
           << "Inventory source: mock fixtures (real discovery begins in 0.3)\n"
           << "External process execution: disabled\n\n"
           << "Capabilities\n";
    for (const CapabilityStatus& capability : snapshot.value().capabilities) {
        output << "  " << capabilityName(capability.id) << ": "
               << capabilityAvailabilityName(capability.availability)
               << " [" << capability.provider << "]\n";
    }

    output << "\nFixture drives\n";
    for (const Drive& drive : snapshot.value().drives) {
        Result<std::string> key = drive.identity.lockKey();
        output << "  " << drive.current_path << "  "
               << driveStatusName(drive.status) << "  "
               << drive.identity.model << "\n"
               << "    identity: " << (key ? key.value() : "INVALID") << '\n';
    }
    output << "\nNo device or storage command was executed.\n";
    return 0;
}

int runApplicationCli(const std::vector<std::string>& arguments,
                      std::string executable_name,
                      std::ostream& output,
                      std::ostream& error_output,
                      DemoUiRunner run_demo_ui) {
    Result<LaunchOptions> options = parseLaunchOptions(arguments);
    if (!options) {
        error_output << options.error().message << '\n'
                     << usage(std::move(executable_name));
        return 1;
    }
    if (options.value().show_help) {
        output << usage(std::move(executable_name));
        return 0;
    }
    if (options.value().show_version) {
        output << "DriveLab " << kVersion << '\n';
        return 0;
    }
    if (options.value().mode == ExecutionMode::DryRun) {
        return runDryRunBootstrap(output, error_output);
    }
    if (options.value().mode == ExecutionMode::Demo) {
        if (run_demo_ui) return run_demo_ui();
        error_output << "Demo UI is not available in this build.\n";
        return 1;
    }

    error_output
        << "Production hardware mode is not available in DriveLab "
        << kVersion << ". Use --demo or --dry-run.\n";
    return 1;
}

}  // namespace drivelab
