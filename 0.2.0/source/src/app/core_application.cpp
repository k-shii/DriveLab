#include "app/core_application.h"

#include <algorithm>
#include <utility>

namespace drivelab {

Result<std::unique_ptr<CoreApplication>> CoreApplication::createMock(
    AppConfig config,
    ILogger& logger) {
    Result<void> valid = config.validate();
    if (!valid) {
        return Result<std::unique_ptr<CoreApplication>>::failure(valid.error());
    }
    if (config.mode == ExecutionMode::Real) {
        return Result<std::unique_ptr<CoreApplication>>::failure({
            ErrorCode::InvalidArgument,
            "CoreApplication",
            "Mock application factory only supports demo and dry-run modes"
        });
    }
    return Result<std::unique_ptr<CoreApplication>>::success(
        std::unique_ptr<CoreApplication>(new CoreApplication(std::move(config), logger)));
}

Result<std::unique_ptr<CoreApplication>> CoreApplication::createDemo(
    AppConfig config,
    ILogger& logger) {
    if (config.mode != ExecutionMode::Demo) {
        return Result<std::unique_ptr<CoreApplication>>::failure({
            ErrorCode::InvalidArgument,
            "CoreApplication",
            "The demo application factory requires demo execution mode"
        });
    }
    Result<void> valid = config.validate();
    if (!valid) {
        return Result<std::unique_ptr<CoreApplication>>::failure(valid.error());
    }
    // The approved demo permits concurrent work on every distinct fixture drive.
    config.max_parallel_drives = std::max(
        config.max_parallel_drives, MockInventoryProvider::fixtureDrives().size());

    std::unique_ptr<CoreApplication> application(
        new CoreApplication(std::move(config), logger));
    Result<std::unique_ptr<DemoSession>> demo = DemoSession::create(
        application->inventory_,
        application->health_,
        application->ata_,
        application->benchmark_,
        application->sanitize_,
        application->jobs_);
    if (!demo) {
        return Result<std::unique_ptr<CoreApplication>>::failure(demo.error());
    }
    application->demo_session_ = std::move(demo.value());
    return Result<std::unique_ptr<CoreApplication>>::success(std::move(application));
}

CoreApplication::CoreApplication(AppConfig config, ILogger& logger)
    : config_(std::move(config)),
      logger_(logger),
      jobs_(config_.mode, config_.max_parallel_drives, inventory_, safety_policy_, logger_),
      process_runner_(config_.mode),
      report_writer_("Report file writes are disabled in mock and dry-run modes") {
    capabilities_.set(inventory_.capability());
    capabilities_.set(health_.capability());
    capabilities_.set(ata_.capability());
    capabilities_.set(benchmark_.capability());
    capabilities_.set(sanitize_.capability());
    capabilities_.set(config_.mode == ExecutionMode::Demo
        ? CapabilityStatus{
              CapabilityId::Latency,
              CapabilityAvailability::Available,
              "mock",
              "Simulated latency profile; no device access"
          }
        : CapabilityStatus{
              CapabilityId::Latency,
              CapabilityAvailability::NotImplemented,
              "none",
              "Latency provider is deferred to the performance milestone"
          });
    capabilities_.set({
        CapabilityId::ProcessExecution,
        CapabilityAvailability::DisabledByMode,
        "guarded",
        "No external process can execute in demo or dry-run mode"
    });
    capabilities_.set({
        CapabilityId::Reporting,
        CapabilityAvailability::DisabledByMode,
        "disabled",
        "Mock modes do not write report files"
    });
}

Result<CoreSnapshot> CoreApplication::snapshot() {
    Result<std::vector<Drive>> scanned = inventory_.scan();
    if (!scanned) return Result<CoreSnapshot>::failure(scanned.error());
    return Result<CoreSnapshot>::success({
        config_.mode,
        scanned.value(),
        capabilities_.all()
    });
}

Result<JobId> CoreApplication::submitHealth(const Drive& drive, HealthTestType test) {
    Result<OperationRequest> operation = health_.plan(drive, test);
    if (!operation) return Result<JobId>::failure(operation.error());
    return jobs_.submit(std::move(operation.value()));
}

Result<JobId> CoreApplication::submitAtaInspection(
    const Drive& drive,
    AtaInspectionType inspection) {
    Result<OperationRequest> operation = ata_.plan(drive, inspection);
    if (!operation) return Result<JobId>::failure(operation.error());
    return jobs_.submit(std::move(operation.value()));
}

Result<JobId> CoreApplication::submitBenchmark(
    const Drive& drive,
    BenchmarkProfile profile) {
    Result<OperationRequest> operation = benchmark_.plan(drive, profile);
    if (!operation) return Result<JobId>::failure(operation.error());
    return jobs_.submit(std::move(operation.value()));
}

Result<JobId> CoreApplication::submitSanitize(const Drive& drive, SanitizeMethod method) {
    Result<OperationRequest> operation = sanitize_.plan(drive, method);
    if (!operation) return Result<JobId>::failure(operation.error());
    return jobs_.submit(std::move(operation.value()));
}

Result<DemoSnapshot> CoreApplication::demoSnapshot() const {
    if (!demo_session_) {
        return Result<DemoSnapshot>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return Result<DemoSnapshot>::success(demo_session_->snapshot());
}

Result<std::vector<DemoFeature>> CoreApplication::demoFeatures(
    const DriveId& drive_id) const {
    if (!demo_session_) {
        return Result<std::vector<DemoFeature>>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->availableFeatures(drive_id);
}

Result<std::vector<DemoWorkflowDefinition>> CoreApplication::demoWorkflows(
    const DriveId& drive_id,
    DemoFeature feature) const {
    if (!demo_session_) {
        return Result<std::vector<DemoWorkflowDefinition>>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->workflows(drive_id, feature);
}

Result<void> CoreApplication::scanDemoInventory() {
    if (!demo_session_) {
        return Result<void>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->scanInventory();
}

Result<DemoWorkflowResult> CoreApplication::runDemoWorkflow(
    const DemoWorkflowCommand& command) {
    if (!demo_session_) {
        return Result<DemoWorkflowResult>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->runWorkflow(command);
}

Result<void> CoreApplication::pauseDemoJob(JobId id) {
    if (!demo_session_) {
        return Result<void>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->pauseJob(id);
}

Result<void> CoreApplication::resumeDemoJob(JobId id) {
    if (!demo_session_) {
        return Result<void>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->resumeJob(id);
}

Result<void> CoreApplication::cancelDemoJob(JobId id) {
    if (!demo_session_) {
        return Result<void>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return demo_session_->cancelJob(id);
}

Result<void> CoreApplication::advanceDemo(std::chrono::milliseconds elapsed) {
    if (!demo_session_) {
        return Result<void>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    demo_session_->advance(elapsed);
    return Result<void>::success();
}

Result<bool> CoreApplication::canTerminateDemo() const {
    if (!demo_session_) {
        return Result<bool>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return Result<bool>::success(demo_session_->canTerminate());
}

Result<bool> CoreApplication::requestDemoTermination() {
    if (!demo_session_) {
        return Result<bool>::failure({
            ErrorCode::Unavailable,
            "CoreApplication",
            "The complete demo state is available only from createDemo"
        });
    }
    return Result<bool>::success(demo_session_->requestTermination());
}

JobManager& CoreApplication::jobs() {
    return jobs_;
}

DisabledProcessRunner& CoreApplication::processRunner() {
    return process_runner_;
}

ReportWriter& CoreApplication::reportWriter() {
    return report_writer_;
}

const CapabilityCatalog& CoreApplication::capabilities() const {
    return capabilities_;
}

}  // namespace drivelab
