#pragma once

#include "core/demo_model.h"
#include "core/result.h"
#include "jobs/job_manager.h"
#include "providers/provider_interfaces.h"

#include <chrono>
#include <memory>
#include <vector>

namespace drivelab {

class DemoSession {
public:
    static Result<std::unique_ptr<DemoSession>> create(
        InventoryProvider& inventory,
        HealthProvider& health,
        AtaProvider& ata,
        BenchmarkProvider& benchmark,
        SanitizeProvider& sanitize,
        JobManager& jobs);

    [[nodiscard]] DemoSnapshot snapshot() const;
    [[nodiscard]] Result<std::vector<DemoFeature>> availableFeatures(
        const DriveId& drive_id) const;
    [[nodiscard]] Result<std::vector<DemoWorkflowDefinition>> workflows(
        const DriveId& drive_id,
        DemoFeature feature) const;

    Result<void> scanInventory();
    Result<DemoWorkflowResult> runWorkflow(const DemoWorkflowCommand& command);
    Result<void> pauseJob(JobId id);
    Result<void> resumeJob(JobId id);
    Result<void> cancelJob(JobId id);
    [[nodiscard]] bool requestTermination();
    void advance(std::chrono::milliseconds elapsed);

    [[nodiscard]] bool canTerminate() const;

private:
    DemoSession(InventoryProvider& inventory,
                HealthProvider& health,
                AtaProvider& ata,
                BenchmarkProvider& benchmark,
                SanitizeProvider& sanitize,
                JobManager& jobs);

    Result<void> initialize();
    Result<void> loadDriveFixtures(std::vector<Drive> drives);
    [[nodiscard]] const DemoDriveDetails* findDrive(const DriveId& drive_id) const;
    [[nodiscard]] Result<OperationRequest> planWorkflow(
        const Drive& drive,
        DemoWorkflowId workflow) const;
    void addEvent(std::string message);
    void tickOnce();
    void syncTrackedProgress();

    InventoryProvider& inventory_;
    HealthProvider& health_;
    AtaProvider& ata_;
    BenchmarkProvider& benchmark_;
    SanitizeProvider& sanitize_;
    JobManager& jobs_;
    std::vector<DemoDriveDetails> drives_;
    std::vector<DemoEvent> events_;
    DemoFeatureProgress smart_progress_;
    DemoFeatureProgress benchmark_progress_;
    std::chrono::milliseconds tick_remainder_{0};
};

}  // namespace drivelab
