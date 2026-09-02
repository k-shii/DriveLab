#pragma once

#include "core/execution_mode.h"
#include "core/result.h"
#include "jobs/job.h"
#include "logging/logger.h"
#include "providers/provider_interfaces.h"
#include "safety/safety_policy.h"

#include <cstddef>
#include <chrono>
#include <map>
#include <optional>
#include <vector>

namespace drivelab {

class JobManager {
public:
    JobManager(ExecutionMode mode,
               std::size_t max_parallel_drives,
               InventoryProvider& inventory,
               const SafetyPolicy& safety_policy,
               ILogger& logger);

    Result<JobId> submit(OperationRequest operation);
    Result<void> updateProgress(
        JobId id,
        int progress_percent,
        std::optional<std::chrono::seconds> estimated_remaining = std::nullopt);
    Result<void> complete(JobId id);
    Result<void> markFailed(JobId id, Error error);
    Result<void> pause(JobId id);
    Result<void> resume(JobId id);
    Result<void> cancel(JobId id);

    [[nodiscard]] const JobRecord* find(JobId id) const;
    [[nodiscard]] const std::vector<JobRecord>& jobs() const;
    [[nodiscard]] std::size_t activeDriveCount() const;
    [[nodiscard]] bool canTerminate() const;

private:
    JobRecord* findMutable(JobId id);
    bool start(JobRecord& job);
    void startEligibleJobs();
    void releaseLock(const JobRecord& job);
    void fail(JobRecord& job, Error error);
    void log(const JobRecord& job, LogSeverity severity, std::string message);
    Result<void> invalidTransition(JobId id, std::string message) const;

    ExecutionMode mode_;
    std::size_t max_parallel_drives_;
    InventoryProvider& inventory_;
    const SafetyPolicy& safety_policy_;
    ILogger& logger_;
    JobId next_id_ = 1;
    std::vector<JobRecord> jobs_;
    std::map<DriveId, JobId> active_locks_;
};

}  // namespace drivelab
