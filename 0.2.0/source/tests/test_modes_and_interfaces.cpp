#include "test_support.h"

#include "app/core_application.h"
#include "app/core_cli.h"
#include "app/launch_options.h"
#include "core/version.h"
#include "process/disabled_process_runner.h"

#include <sstream>

using namespace drivelab;

int main() {
    return test::run([] {
        Result<LaunchOptions> demo = parseLaunchOptions({"--demo"});
        DL_CHECK(demo);
        DL_CHECK(demo.value().mode == ExecutionMode::Demo);
        Result<LaunchOptions> dry_run = parseLaunchOptions({"--dry-run"});
        DL_CHECK(dry_run);
        DL_CHECK(dry_run.value().mode == ExecutionMode::DryRun);
        Result<LaunchOptions> help = parseLaunchOptions({"--help"});
        DL_CHECK(help && help.value().show_help);
        Result<LaunchOptions> version = parseLaunchOptions({"--version"});
        DL_CHECK(version && version.value().show_version);
        DL_CHECK(!parseLaunchOptions({"--demo", "--dry-run"}));
        DL_CHECK(!parseLaunchOptions({"--unknown"}));

        int demo_calls = 0;
        const DemoUiRunner demo_runner = [&] {
            ++demo_calls;
            return 23;
        };

        std::ostringstream demo_output;
        std::ostringstream demo_errors;
        DL_CHECK(runApplicationCli({"--demo"}, "drivelab", demo_output,
                                   demo_errors, demo_runner) == 23);
        DL_CHECK(demo_calls == 1);
        DL_CHECK(demo_output.str().empty());
        DL_CHECK(demo_errors.str().empty());

        std::ostringstream help_output;
        std::ostringstream help_errors;
        DL_CHECK(runApplicationCli({"--help"}, "drivelab", help_output,
                                   help_errors, demo_runner) == 0);
        DL_CHECK(help_output.str().find("Usage: drivelab") != std::string::npos);
        DL_CHECK(help_errors.str().empty());
        DL_CHECK(demo_calls == 1);

        std::ostringstream version_output;
        std::ostringstream version_errors;
        DL_CHECK(runApplicationCli({"--version"}, "drivelab", version_output,
                                   version_errors, demo_runner) == 0);
        DL_CHECK(version_output.str() ==
                 "DriveLab " + std::string(kVersion) + "\n");
        DL_CHECK(version_errors.str().empty());
        DL_CHECK(demo_calls == 1);

        std::ostringstream dry_run_output;
        std::ostringstream dry_run_errors;
        DL_CHECK(runApplicationCli({"--dry-run"}, "drivelab", dry_run_output,
                                   dry_run_errors, demo_runner) == 0);
        DL_CHECK(dry_run_errors.str().empty());
        DL_CHECK(dry_run_output.str().find("External process execution: disabled") !=
                 std::string::npos);
        DL_CHECK(dry_run_output.str().find("No device or storage command was executed") !=
                 std::string::npos);
        DL_CHECK(demo_calls == 1);

        std::ostringstream normal_output;
        std::ostringstream normal_errors;
        DL_CHECK(runApplicationCli({}, "drivelab", normal_output,
                                   normal_errors, demo_runner) == 1);
        DL_CHECK(normal_output.str().empty());
        DL_CHECK(normal_errors.str() ==
                 "Production hardware mode is not available in DriveLab " +
                     std::string(kVersion) + ". Use --demo or --dry-run.\n");
        DL_CHECK(demo_calls == 1);

        DisabledProcessRunner process_runner(ExecutionMode::DryRun);
        ProcessSpec process;
        process.executable = "fio";
        process.arguments = {"--filename=/dev/sdd", "--readonly"};
        Result<ProcessResult> process_result = process_runner.run(process);
        DL_CHECK(!process_result);
        DL_CHECK(process_result.error().code == ErrorCode::ExecutionDisabled);
        DL_CHECK(process_runner.planned().size() == 1);
        DL_CHECK(renderProcessSpec(process).find("--readonly") != std::string::npos);

        AppConfig invalid_config;
        invalid_config.max_parallel_drives = 0;
        DL_CHECK(!invalid_config.validate());

        SafetyPolicy safety;
        const std::vector<Drive> fixtures = MockInventoryProvider::fixtureDrives();
        Result<ExecutionAuthorization> protected_status = safety.authorize(
            fixtures[2], SafetyClass::StatusOnly, ExecutionMode::Real);
        DL_CHECK(protected_status);
        DL_CHECK(protected_status.value().may_execute);
        Result<ExecutionAuthorization> real_destructive = safety.authorize(
            fixtures[0], SafetyClass::Destructive, ExecutionMode::Real);
        DL_CHECK(!real_destructive);
        DL_CHECK(real_destructive.error().code == ErrorCode::Unavailable);

        NullLogger logger;
        AppConfig demo_config;
        demo_config.mode = ExecutionMode::Demo;
        Result<std::unique_ptr<CoreApplication>> application =
            CoreApplication::createMock(demo_config, logger);
        DL_CHECK(application);
        Result<CoreSnapshot> snapshot = application.value()->snapshot();
        DL_CHECK(snapshot);
        DL_CHECK(snapshot.value().drives.size() == 7);
        DL_CHECK(snapshot.value().capabilities.size() == 8);

        Result<JobId> protected_benchmark = application.value()->submitBenchmark(
            snapshot.value().drives[2], BenchmarkProfile::QuickRead);
        DL_CHECK(!protected_benchmark);
        Result<JobId> demo_benchmark = application.value()->submitBenchmark(
            snapshot.value().drives[0], BenchmarkProfile::QuickRead);
        DL_CHECK(demo_benchmark);
        DL_CHECK(application.value()->jobs().find(demo_benchmark.value())->state ==
                 JobState::Running);

        Report report;
        report.drive_identity = snapshot.value().drives[0].identity;
        report.title = "Demo report";
        DL_CHECK(!application.value()->reportWriter().write(report));
    });
}
