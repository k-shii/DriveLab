#pragma once

#include "app/demo_session.h"
#include "config/app_config.h"
#include "core/capability.h"
#include "core/drive.h"
#include "core/result.h"
#include "jobs/job_manager.h"
#include "logging/logger.h"
#include "process/disabled_process_runner.h"
#include "providers/mock_providers.h"
#include "report/report_writer.h"
#include "safety/safety_policy.h"

#include <memory>
#include <chrono>
#include <vector>

namespace drivelab {

struct CoreSnapshot {
    ExecutionMode mode = ExecutionMode::Demo;
    std::vector<Drive> drives;
    std::vector<CapabilityStatus> capabilities;
};

class CoreApplication {
public:
    static Result<std::unique_ptr<CoreApplication>> createMock(
        AppConfig config,
        ILogger& logger);
    static Result<std::unique_ptr<CoreApplication>> createDemo(
        AppConfig config,
        ILogger& logger);

    Result<CoreSnapshot> snapshot();
    Result<JobId> submitHealth(const Drive& drive, HealthTestType test);
    Result<JobId> submitAtaInspection(const Drive& drive, AtaInspectionType inspection);
    Result<JobId> submitBenchmark(const Drive& drive, BenchmarkProfile profile);
    Result<JobId> submitSanitize(const Drive& drive, SanitizeMethod method);

    [[nodiscard]] Result<DemoSnapshot> demoSnapshot() const;
    [[nodiscard]] Result<std::vector<DemoFeature>> demoFeatures(
        const DriveId& drive_id) const;
    [[nodiscard]] Result<std::vector<DemoWorkflowDefinition>> demoWorkflows(
        const DriveId& drive_id,
        DemoFeature feature) const;
    Result<void> scanDemoInventory();
    Result<DemoWorkflowResult> runDemoWorkflow(const DemoWorkflowCommand& command);
    Result<void> pauseDemoJob(JobId id);
    Result<void> resumeDemoJob(JobId id);
    Result<void> cancelDemoJob(JobId id);
    Result<void> advanceDemo(std::chrono::milliseconds elapsed);
    [[nodiscard]] Result<bool> canTerminateDemo() const;
    [[nodiscard]] Result<bool> requestDemoTermination();

    [[nodiscard]] JobManager& jobs();
    [[nodiscard]] DisabledProcessRunner& processRunner();
    [[nodiscard]] ReportWriter& reportWriter();
    [[nodiscard]] const CapabilityCatalog& capabilities() const;

private:
    CoreApplication(AppConfig config, ILogger& logger);

    AppConfig config_;
    ILogger& logger_;
    MockInventoryProvider inventory_;
    MockHealthProvider health_;
    MockAtaProvider ata_;
    MockBenchmarkProvider benchmark_;
    MockSanitizeProvider sanitize_;
    SafetyPolicy safety_policy_;
    JobManager jobs_;
    DisabledProcessRunner process_runner_;
    DisabledReportWriter report_writer_;
    CapabilityCatalog capabilities_;
    std::unique_ptr<DemoSession> demo_session_;
};

}  // namespace drivelab
