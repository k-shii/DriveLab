#include "jobs/job_manager.h"

#include <algorithm>
#include <utility>

namespace drivelab {

std::string jobStateName(JobState state) {
    switch (state) {
        case JobState::Planned: return "PLANNED";
        case JobState::Queued: return "QUEUED";
        case JobState::Starting: return "STARTING";
        case JobState::Running: return "RUNNING";
        case JobState::Paused: return "PAUSED";
        case JobState::Completed: return "COMPLETED";
        case JobState::Cancelled: return "CANCELLED";
        case JobState::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

JobManager::JobManager(ExecutionMode mode,
                       std::size_t max_parallel_drives,
                       InventoryProvider& inventory,
                       const SafetyPolicy& safety_policy,
                       ILogger& logger)
    : mode_(mode),
      max_parallel_drives_(std::max<std::size_t>(1, max_parallel_drives)),
      inventory_(inventory),
      safety_policy_(safety_policy),
      logger_(logger) {}

Result<JobId> JobManager::submit(OperationRequest operation) {
    Result<DriveId> drive_id = operation.target.identity.driveId();
    if (!drive_id) return Result<JobId>::failure(drive_id.error());

    Result<ExecutionAuthorization> authorization = safety_policy_.authorize(
        operation.target, operation.safety_class, mode_);
    if (!authorization) return Result<JobId>::failure(authorization.error());

    JobRecord job;
    job.id = next_id_++;
    job.operation = std::move(operation);
    job.drive_id = drive_id.value();
    job.observed_path = job.operation.target.current_path;
    job.state = authorization.value().planned_only ? JobState::Planned : JobState::Queued;
    jobs_.push_back(std::move(job));
    JobRecord& submitted = jobs_.back();

    if (submitted.state == JobState::Planned) {
        log(submitted, LogSeverity::Info, "Operation recorded without execution in dry-run mode");
        return Result<JobId>::success(submitted.id);
    }

    if (active_locks_.contains(submitted.drive_id) ||
        active_locks_.size() >= max_parallel_drives_) {
        log(submitted, LogSeverity::Info, "Job queued");
    } else {
        start(submitted);
    }
    return Result<JobId>::success(submitted.id);
}

Result<void> JobManager::updateProgress(
    JobId id,
    int progress_percent,
    std::optional<std::chrono::seconds> estimated_remaining) {
    JobRecord* job = findMutable(id);
    if (!job || (job->state != JobState::Running && job->state != JobState::Paused)) {
        return invalidTransition(id, "Only running or paused jobs can report progress");
    }
    if (progress_percent < 0 || progress_percent > 100) {
        return Result<void>::failure({
            ErrorCode::InvalidArgument,
            "JobManager Job#" + std::to_string(id),
            "Progress must be between 0 and 100"
        });
    }
    job->progress_percent = progress_percent;
    job->estimated_remaining = estimated_remaining;
    return Result<void>::success();
}

Result<void> JobManager::complete(JobId id) {
    JobRecord* job = findMutable(id);
    if (!job || (job->state != JobState::Running && job->state != JobState::Paused)) {
        return invalidTransition(id, "Only running or paused jobs can complete");
    }
    job->state = JobState::Completed;
    job->progress_percent = 100;
    job->estimated_remaining = std::chrono::seconds{0};
    job->finished_at = std::chrono::system_clock::now();
    releaseLock(*job);
    log(*job, LogSeverity::Info, "Job completed");
    startEligibleJobs();
    return Result<void>::success();
}

Result<void> JobManager::markFailed(JobId id, Error error) {
    JobRecord* job = findMutable(id);
    if (!job || (job->state != JobState::Running && job->state != JobState::Paused &&
                 job->state != JobState::Starting)) {
        return invalidTransition(id, "Only active jobs can fail");
    }
    releaseLock(*job);
    fail(*job, std::move(error));
    startEligibleJobs();
    return Result<void>::success();
}

Result<void> JobManager::pause(JobId id) {
    JobRecord* job = findMutable(id);
    if (!job || job->state != JobState::Running || !job->operation.controls.can_pause) {
        return invalidTransition(id, "Job cannot be paused in its current state");
    }
    job->state = JobState::Paused;
    log(*job, LogSeverity::Info, "Job paused; physical-drive lock retained");
    return Result<void>::success();
}

Result<void> JobManager::resume(JobId id) {
    JobRecord* job = findMutable(id);
    if (!job || job->state != JobState::Paused || !job->operation.controls.can_resume) {
        return invalidTransition(id, "Job cannot be resumed in its current state");
    }
    job->state = JobState::Running;
    log(*job, LogSeverity::Info, "Job resumed");
    return Result<void>::success();
}

Result<void> JobManager::cancel(JobId id) {
    JobRecord* job = findMutable(id);
    if (!job) return invalidTransition(id, "Job does not exist");
    if (job->state == JobState::Queued) {
        job->state = JobState::Cancelled;
        job->estimated_remaining.reset();
        job->finished_at = std::chrono::system_clock::now();
        log(*job, LogSeverity::Info, "Queued job cancelled");
        return Result<void>::success();
    }
    if ((job->state == JobState::Running || job->state == JobState::Paused) &&
        job->operation.controls.can_cancel) {
        job->state = JobState::Cancelled;
        job->estimated_remaining.reset();
        job->finished_at = std::chrono::system_clock::now();
        releaseLock(*job);
        log(*job, LogSeverity::Info, "Active job cancelled");
        startEligibleJobs();
        return Result<void>::success();
    }
    return invalidTransition(id, "Job cannot be cancelled in its current state");
}

const JobRecord* JobManager::find(JobId id) const {
    auto job = std::find_if(jobs_.begin(), jobs_.end(), [&](const JobRecord& record) {
        return record.id == id;
    });
    return job == jobs_.end() ? nullptr : &*job;
}

const std::vector<JobRecord>& JobManager::jobs() const {
    return jobs_;
}

std::size_t JobManager::activeDriveCount() const {
    return active_locks_.size();
}

bool JobManager::canTerminate() const {
    return std::none_of(jobs_.begin(), jobs_.end(), [](const JobRecord& job) {
        return (job.state == JobState::Running || job.state == JobState::Paused) &&
               !job.operation.controls.can_cancel;
    });
}

JobRecord* JobManager::findMutable(JobId id) {
    auto job = std::find_if(jobs_.begin(), jobs_.end(), [&](const JobRecord& record) {
        return record.id == id;
    });
    return job == jobs_.end() ? nullptr : &*job;
}

bool JobManager::start(JobRecord& job) {
    job.state = JobState::Starting;
    Result<Drive> observed = inventory_.resolve(job.operation.target.identity);
    if (!observed) {
        fail(job, observed.error());
        return false;
    }
    if (!job.operation.target.identity.samePhysicalDevice(observed.value().identity)) {
        fail(job, {
            ErrorCode::IdentityMismatch,
            "JobManager",
            "Physical drive identity changed before execution"
        });
        return false;
    }

    Result<ExecutionAuthorization> authorization = safety_policy_.authorize(
        observed.value(), job.operation.safety_class, mode_);
    if (!authorization) {
        fail(job, authorization.error());
        return false;
    }
    if (!authorization.value().may_execute) {
        job.state = JobState::Planned;
        log(job, LogSeverity::Info, "Execution suppressed by mode");
        return false;
    }

    job.observed_path = observed.value().current_path;
    job.started_at = std::chrono::system_clock::now();
    job.state = JobState::Running;
    active_locks_[job.drive_id] = job.id;
    log(job, LogSeverity::Info,
        authorization.value().simulated ? "Simulated job started" : "Job started");
    return true;
}

void JobManager::startEligibleJobs() {
    bool made_progress = true;
    while (made_progress && active_locks_.size() < max_parallel_drives_) {
        made_progress = false;
        for (JobRecord& job : jobs_) {
            if (job.state != JobState::Queued || active_locks_.contains(job.drive_id)) {
                continue;
            }
            start(job);
            made_progress = true;
            break;
        }
    }
}

void JobManager::releaseLock(const JobRecord& job) {
    auto lock = active_locks_.find(job.drive_id);
    if (lock != active_locks_.end() && lock->second == job.id) active_locks_.erase(lock);
}

void JobManager::fail(JobRecord& job, Error error) {
    job.state = JobState::Failed;
    job.estimated_remaining.reset();
    job.finished_at = std::chrono::system_clock::now();
    job.failure = std::move(error);
    log(job, LogSeverity::Error, job.failure->message);
}

void JobManager::log(const JobRecord& job, LogSeverity severity, std::string message) {
    logger_.log({
        std::chrono::system_clock::now(),
        severity,
        "JobManager",
        job.id,
        std::move(message)
    });
}

Result<void> JobManager::invalidTransition(JobId id, std::string message) const {
    return Result<void>::failure({
        ErrorCode::InvalidState,
        "JobManager Job#" + std::to_string(id),
        std::move(message)
    });
}

}  // namespace drivelab
