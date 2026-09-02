#include "app/demo_session.h"

#include <algorithm>
#include <map>
#include <utility>

namespace drivelab {
namespace {

constexpr std::chrono::milliseconds kDemoTick{500};
constexpr std::size_t kMaxDemoEvents = 50;

std::vector<DemoBenchmarkResult> benchmarkResults() {
    return {
        {DemoWorkflowId::BenchmarkQuickSequential,
         "184.6 MiB/s avg . 192.3 MiB/s peak"},
        {DemoWorkflowId::BenchmarkHddZoneCharacterization,
         "Outer 189 . Middle 162 . Inner 108 MiB/s"},
        {DemoWorkflowId::BenchmarkRandom4kQd1, "183 IOPS . p99 24.1 ms"},
        {DemoWorkflowId::BenchmarkRandom4kQd32, "1,612 IOPS . p99 18.7 ms"},
        {DemoWorkflowId::BenchmarkLatency,
         "avg 7.4 ms . p95 12.8 ms . p99 24.1 ms"}
    };
}

DemoDriveDetails details(
    Drive drive,
    std::string display_capacity,
    std::string transport,
    std::string health,
    int temperature,
    std::uint64_t power_on_hours,
    std::uint64_t reallocated,
    std::uint64_t pending,
    std::uint64_t offline_uncorrectable,
    std::string ata_security,
    std::string hpa,
    std::string security,
    std::string dco,
    bool erase_supported) {
    const std::uint64_t capacity = drive.identity.capacity_bytes;
    const bool hpa_capacity_available = hpa != "Not applicable" && hpa != "Status only";
    DemoDriveDetails value;
    value.drive = std::move(drive);
    value.display_capacity = std::move(display_capacity);
    value.transport = std::move(transport);
    value.smart = {
        std::move(health), temperature, power_on_hours, reallocated, pending,
        offline_uncorrectable
    };
    value.ata = {
        std::move(ata_security),
        std::move(hpa),
        std::move(security),
        hpa_capacity_available ? std::optional<std::uint64_t>{capacity} : std::nullopt,
        hpa_capacity_available ? std::optional<std::uint64_t>{capacity} : std::nullopt,
        std::move(dco),
        erase_supported,
        erase_supported,
        erase_supported ? std::optional<std::chrono::minutes>{std::chrono::minutes{102}}
                        : std::nullopt,
        erase_supported ? std::optional<std::chrono::minutes>{std::chrono::minutes{78}}
                        : std::nullopt
    };
    value.benchmark_results = benchmarkResults();
    return value;
}

std::optional<std::chrono::seconds> etaForProgress(int progress) {
    return std::chrono::seconds{std::max(0, (100 - progress) / 4)};
}

bool isHealthOperation(OperationKind kind) {
    return kind == OperationKind::HealthShortTest ||
           kind == OperationKind::HealthExtendedTest ||
           kind == OperationKind::HealthConveyanceTest;
}

}  // namespace

Result<std::unique_ptr<DemoSession>> DemoSession::create(
    InventoryProvider& inventory,
    HealthProvider& health,
    AtaProvider& ata,
    BenchmarkProvider& benchmark,
    SanitizeProvider& sanitize,
    JobManager& jobs) {
    std::unique_ptr<DemoSession> session(new DemoSession(
        inventory, health, ata, benchmark, sanitize, jobs));
    Result<void> initialized = session->initialize();
    if (!initialized) {
        return Result<std::unique_ptr<DemoSession>>::failure(initialized.error());
    }
    return Result<std::unique_ptr<DemoSession>>::success(std::move(session));
}

DemoSession::DemoSession(InventoryProvider& inventory,
                         HealthProvider& health,
                         AtaProvider& ata,
                         BenchmarkProvider& benchmark,
                         SanitizeProvider& sanitize,
                         JobManager& jobs)
    : inventory_(inventory),
      health_(health),
      ata_(ata),
      benchmark_(benchmark),
      sanitize_(sanitize),
      jobs_(jobs) {}

Result<void> DemoSession::initialize() {
    Result<std::vector<Drive>> scanned = inventory_.scan();
    if (!scanned) return Result<void>::failure(scanned.error());
    Result<void> drives_loaded = loadDriveFixtures(std::move(scanned.value()));
    if (!drives_loaded) return drives_loaded;

    const auto now = std::chrono::system_clock::now();
    events_ = {
        {now, "Demo session started: 7 canned drives loaded; no device commands executed"}
    };

    Result<DriveId> first_id = drives_[0].drive.identity.driveId();
    if (!first_id) return Result<void>::failure(first_id.error());
    smart_progress_ = {
        DemoWorkflowId::SmartExtendedTest,
        first_id.value(),
        drives_[0].drive.current_path,
        JobState::Planned,
        0,
        std::nullopt
    };
    benchmark_progress_ = {
        DemoWorkflowId::BenchmarkQuickSequential,
        first_id.value(),
        drives_[0].drive.current_path,
        JobState::Planned,
        0,
        std::nullopt
    };
    return Result<void>::success();
}

Result<void> DemoSession::loadDriveFixtures(std::vector<Drive> drives) {
    if (drives.size() != 7) {
        return Result<void>::failure({
            ErrorCode::InvalidArgument,
            "DemoSession",
            "The approved demo fixture requires exactly seven drives"
        });
    }

    drives_.clear();
    drives_.reserve(drives.size());
    drives_.push_back(details(
        std::move(drives[0]), "596.2 GiB", "SATA 3 Gb/s", "PASSED", 35, 37256,
        0, 0, 0, "Supported - Disabled", "Disabled - native max exposed",
        "Not Frozen", "Unrestricted", true));
    drives_.push_back(details(
        std::move(drives[1]), "3.64 TiB", "SAS 6 Gb/s", "PASSED", 31, 18204,
        0, 0, 0, "Supported", "Not applicable", "Unlocked", "Not applicable",
        true));
    drives_.push_back(details(
        std::move(drives[2]), "1.82 TiB", "SATA 6 Gb/s", "PASSED", 33, 42891,
        0, 0, 0, "Status only", "Status only", "Not exposed", "Status only",
        false));
    drives_.push_back(details(
        std::move(drives[3]), "223.6 GiB", "SATA 6 Gb/s", "PASSED", 29, 9842,
        0, 0, 0, "Status only", "Status only", "Not exposed", "Status only",
        false));
    drives_.push_back(details(
        std::move(drives[4]), "7.28 TiB", "SATA 6 Gb/s", "PASSED", 36, 22106,
        0, 0, 0, "Status only", "Status only", "Not exposed", "Status only",
        false));
    drives_.push_back(details(
        std::move(drives[5]), "1.82 TiB", "PCIe 4.0 x4", "FAILED", 57, 16772,
        18, 7, 3, "Not applicable", "Not applicable", "Not applicable",
        "Not applicable", false));
    drives_.push_back(details(
        std::move(drives[6]), "931.5 GiB", "SATA 6 Gb/s", "PASSED", 42, 31449,
        12, 0, 0, "Supported - Disabled", "Disabled", "Not Frozen",
        "Unrestricted", true));
    return Result<void>::success();
}

DemoSnapshot DemoSession::snapshot() const {
    std::vector<JobRecord> jobs = jobs_.jobs();
    std::sort(jobs.begin(), jobs.end(), [](const JobRecord& left, const JobRecord& right) {
        return left.id > right.id;
    });
    return {drives_, events_, std::move(jobs), smart_progress_, benchmark_progress_};
}

Result<std::vector<DemoFeature>> DemoSession::availableFeatures(
    const DriveId& drive_id) const {
    const DemoDriveDetails* drive = findDrive(drive_id);
    if (!drive) {
        return Result<std::vector<DemoFeature>>::failure({
            ErrorCode::NotFound,
            "DemoSession",
            "Demo drive was not found"
        });
    }
    if (drive->drive.isProtected()) {
        return Result<std::vector<DemoFeature>>::success(
            {DemoFeature::Overview, DemoFeature::Smart});
    }
    return Result<std::vector<DemoFeature>>::success({
        DemoFeature::Overview,
        DemoFeature::Smart,
        DemoFeature::AtaHpa,
        DemoFeature::Benchmark,
        DemoFeature::Sanitize
    });
}

Result<std::vector<DemoWorkflowDefinition>> DemoSession::workflows(
    const DriveId& drive_id,
    DemoFeature feature) const {
    const DemoDriveDetails* drive = findDrive(drive_id);
    if (!drive) {
        return Result<std::vector<DemoWorkflowDefinition>>::failure({
            ErrorCode::NotFound,
            "DemoSession",
            "Demo drive was not found"
        });
    }
    if (drive->drive.isProtected() || feature == DemoFeature::Overview) {
        return Result<std::vector<DemoWorkflowDefinition>>::success({});
    }

    std::vector<DemoWorkflowDefinition> result;
    for (const DemoWorkflowDefinition& workflow : demoWorkflowCatalog()) {
        if (workflow.feature != feature) continue;
        DemoWorkflowDefinition available = workflow;
        if (available.id == DemoWorkflowId::SmartConveyanceTest &&
            drive->drive.media != MediaKind::Hdd) {
            available.supported = false;
        }
        result.push_back(std::move(available));
    }
    return Result<std::vector<DemoWorkflowDefinition>>::success(std::move(result));
}

Result<void> DemoSession::scanInventory() {
    Result<std::vector<Drive>> scanned = inventory_.scan();
    if (!scanned) return Result<void>::failure(scanned.error());
    for (const Drive& observed : scanned.value()) {
        Result<DriveId> observed_id = observed.identity.driveId();
        if (!observed_id) return Result<void>::failure(observed_id.error());
        auto existing = std::find_if(
            drives_.begin(), drives_.end(), [&](const DemoDriveDetails& details) {
                Result<DriveId> current_id = details.drive.identity.driveId();
                return current_id && current_id.value() == observed_id.value();
            });
        if (existing != drives_.end()) existing->drive = observed;
    }
    addEvent("Demo scan complete: 7 canned drives; no commands executed");
    return Result<void>::success();
}

Result<DemoWorkflowResult> DemoSession::runWorkflow(
    const DemoWorkflowCommand& command) {
    const DemoDriveDetails* details = findDrive(command.drive_id);
    if (!details) {
        return Result<DemoWorkflowResult>::failure({
            ErrorCode::NotFound,
            "DemoSession",
            "Demo drive was not found"
        });
    }
    if (details->drive.isProtected()) {
        return Result<DemoWorkflowResult>::failure({
            ErrorCode::SafetyBlocked,
            "DemoSession",
            "Protected drives expose status only"
        });
    }

    const DemoWorkflowDefinition* definition = findDemoWorkflow(command.workflow);
    if (!definition) {
        return Result<DemoWorkflowResult>::failure({
            ErrorCode::InvalidArgument,
            "DemoSession",
            "Unknown demo workflow"
        });
    }
    Result<std::vector<DemoWorkflowDefinition>> available = workflows(
        command.drive_id, definition->feature);
    if (!available) return Result<DemoWorkflowResult>::failure(available.error());
    auto supported = std::find_if(
        available.value().begin(), available.value().end(), [&](const auto& workflow) {
            return workflow.id == command.workflow && workflow.supported;
        });
    if (supported == available.value().end()) {
        return Result<DemoWorkflowResult>::failure({
            ErrorCode::Unavailable,
            "DemoSession",
            "The selected workflow is unavailable for this demo drive"
        });
    }

    Result<OperationRequest> operation = planWorkflow(details->drive, command.workflow);
    if (!operation) return Result<DemoWorkflowResult>::failure(operation.error());

    std::string message;
    if (definition->behavior == DemoWorkflowBehavior::Inspection) {
        switch (command.workflow) {
            case DemoWorkflowId::SmartRawData:
                message = "Raw S.M.A.R.T demo opened for " +
                          details->drive.current_path + "; no command executed";
                break;
            case DemoWorkflowId::AtaSecurityState:
                message = "ATA Security inspection simulated for " +
                          details->drive.current_path;
                break;
            case DemoWorkflowId::AtaHpaInspect:
                message = "HPA inspection simulated for " + details->drive.current_path;
                break;
            case DemoWorkflowId::AtaDcoInspect:
                message = "DCO identify simulated for " + details->drive.current_path;
                break;
            default:
                message = definition->label + " inspection simulated for " +
                          details->drive.current_path;
                break;
        }
        addEvent(message);
        return Result<DemoWorkflowResult>::success({
            DemoWorkflowOutcome::InspectionCompleted, std::nullopt, std::move(message)
        });
    }

    if (definition->feature == DemoFeature::Smart) {
        operation.value().name = "S.M.A.R.T " + definition->label + " demo";
    } else if (definition->feature == DemoFeature::AtaHpa ||
               definition->feature == DemoFeature::Sanitize) {
        operation.value().name = definition->label + " workflow demo";
    } else {
        operation.value().name = definition->label + " demo";
    }

    Result<JobId> submitted = jobs_.submit(std::move(operation.value()));
    if (!submitted) return Result<DemoWorkflowResult>::failure(submitted.error());
    const JobRecord* job = jobs_.find(submitted.value());
    if (job && job->state == JobState::Running) {
        Result<void> progress = jobs_.updateProgress(
            job->id, 0, std::chrono::seconds{25});
        if (!progress) return Result<DemoWorkflowResult>::failure(progress.error());
    }

    message = jobs_.find(submitted.value())->operation.name + " created for " +
              details->drive.current_path + " (simulation only)";
    addEvent(message);
    if (definition->feature == DemoFeature::Smart) {
        smart_progress_ = {
            command.workflow,
            command.drive_id,
            details->drive.current_path,
            jobs_.find(submitted.value())->state,
            0,
            submitted.value()
        };
    } else if (definition->feature == DemoFeature::Benchmark) {
        benchmark_progress_ = {
            command.workflow,
            command.drive_id,
            details->drive.current_path,
            jobs_.find(submitted.value())->state,
            0,
            submitted.value()
        };
    }
    return Result<DemoWorkflowResult>::success({
        DemoWorkflowOutcome::JobSubmitted, submitted.value(), std::move(message)
    });
}

Result<void> DemoSession::pauseJob(JobId id) {
    Result<void> paused = jobs_.pause(id);
    if (!paused) return paused;
    addEvent("Job #" + std::to_string(id) + " paused");
    syncTrackedProgress();
    return Result<void>::success();
}

Result<void> DemoSession::resumeJob(JobId id) {
    Result<void> resumed = jobs_.resume(id);
    if (!resumed) return resumed;
    addEvent("Job #" + std::to_string(id) + " resumed");
    syncTrackedProgress();
    return Result<void>::success();
}

Result<void> DemoSession::cancelJob(JobId id) {
    const JobRecord* before = jobs_.find(id);
    if (!before) {
        return Result<void>::failure({
            ErrorCode::NotFound,
            "DemoSession",
            "Demo job was not found"
        });
    }
    const JobState previous_state = before->state;
    std::vector<JobId> queued_before;
    for (const JobRecord& job : jobs_.jobs()) {
        if (job.state == JobState::Queued) queued_before.push_back(job.id);
    }
    Result<void> cancelled = jobs_.cancel(id);
    if (!cancelled) return cancelled;
    if (previous_state == JobState::Queued) {
        addEvent("Queued job #" + std::to_string(id) + " cancelled");
    } else {
        addEvent("Job #" + std::to_string(id) + " stopped");
    }
    for (JobId queued_id : queued_before) {
        const JobRecord* queued = jobs_.find(queued_id);
        if (queued && queued->state == JobState::Running) {
            jobs_.updateProgress(queued_id, queued->progress_percent,
                                 std::chrono::seconds{25});
            addEvent("Queued job #" + std::to_string(queued_id) + " started");
        }
    }
    syncTrackedProgress();
    return Result<void>::success();
}

bool DemoSession::requestTermination() {
    if (canTerminate()) return true;
    addEvent("Session termination blocked by non-cancellable job");
    return false;
}

void DemoSession::advance(std::chrono::milliseconds elapsed) {
    if (elapsed.count() <= 0) return;
    tick_remainder_ += elapsed;
    while (tick_remainder_ >= kDemoTick) {
        tick_remainder_ -= kDemoTick;
        tickOnce();
    }
}

bool DemoSession::canTerminate() const {
    return jobs_.canTerminate();
}

const DemoDriveDetails* DemoSession::findDrive(const DriveId& drive_id) const {
    auto found = std::find_if(
        drives_.begin(), drives_.end(), [&](const DemoDriveDetails& details) {
            Result<DriveId> candidate = details.drive.identity.driveId();
            return candidate && candidate.value() == drive_id;
        });
    return found == drives_.end() ? nullptr : &*found;
}

Result<OperationRequest> DemoSession::planWorkflow(
    const Drive& drive,
    DemoWorkflowId workflow) const {
    switch (workflow) {
        case DemoWorkflowId::SmartShortTest:
            return health_.plan(drive, HealthTestType::Short);
        case DemoWorkflowId::SmartExtendedTest:
            return health_.plan(drive, HealthTestType::Extended);
        case DemoWorkflowId::SmartConveyanceTest:
            return health_.plan(drive, HealthTestType::Conveyance);
        case DemoWorkflowId::SmartRawData:
            return health_.plan(drive, HealthTestType::RawData);
        case DemoWorkflowId::AtaSecurityState:
            return ata_.plan(drive, AtaInspectionType::Security);
        case DemoWorkflowId::AtaHpaInspect:
            return ata_.plan(drive, AtaInspectionType::Hpa);
        case DemoWorkflowId::AtaDcoInspect:
            return ata_.plan(drive, AtaInspectionType::Dco);
        case DemoWorkflowId::AtaSecureErase:
        case DemoWorkflowId::SanitizeAtaSecureErase:
            return sanitize_.plan(drive, SanitizeMethod::SecureErase);
        case DemoWorkflowId::AtaEnhancedSecureErase:
        case DemoWorkflowId::SanitizeAtaEnhancedSecureErase:
            return sanitize_.plan(drive, SanitizeMethod::EnhancedSecureErase);
        case DemoWorkflowId::BenchmarkQuickSequential:
            return benchmark_.plan(drive, BenchmarkProfile::QuickRead);
        case DemoWorkflowId::BenchmarkHddZoneCharacterization:
            return benchmark_.plan(drive, BenchmarkProfile::HddCharacterization);
        case DemoWorkflowId::BenchmarkRandom4kQd1:
            return benchmark_.plan(drive, BenchmarkProfile::Random4kQd1);
        case DemoWorkflowId::BenchmarkRandom4kQd32:
            return benchmark_.plan(drive, BenchmarkProfile::Random4kQd32);
        case DemoWorkflowId::BenchmarkLatency:
            return benchmark_.plan(drive, BenchmarkProfile::Latency);
        case DemoWorkflowId::SanitizeNwipeZeroFill:
            return sanitize_.plan(drive, SanitizeMethod::Overwrite);
        case DemoWorkflowId::SanitizeNwipeOneFill:
            return sanitize_.plan(drive, SanitizeMethod::OverwriteOnes);
        case DemoWorkflowId::SanitizeNwipeRandomFill:
            return sanitize_.plan(drive, SanitizeMethod::OverwriteRandom);
    }
    return Result<OperationRequest>::failure({
        ErrorCode::InvalidArgument,
        "DemoSession",
        "Unknown demo workflow"
    });
}

void DemoSession::addEvent(std::string message) {
    events_.insert(events_.begin(), {
        std::chrono::system_clock::now(),
        std::move(message)
    });
    if (events_.size() > kMaxDemoEvents) events_.resize(kMaxDemoEvents);
}

void DemoSession::tickOnce() {
    std::map<JobId, JobState> before;
    std::vector<JobId> running;
    for (const JobRecord& job : jobs_.jobs()) {
        before[job.id] = job.state;
        if (job.state == JobState::Running) running.push_back(job.id);
    }

    for (JobId id : running) {
        const JobRecord* job = jobs_.find(id);
        if (!job || job->state != JobState::Running) continue;
        const int progress = std::min(100, job->progress_percent + 2);
        if (job->operation.target.status == DriveStatus::Failing &&
            isHealthOperation(job->operation.kind) && progress >= 20) {
            Result<void> updated = jobs_.updateProgress(
                id, progress, etaForProgress(progress));
            if (!updated) continue;
            Result<void> failed = jobs_.markFailed(id, {
                ErrorCode::ProcessFailure,
                "mock-health",
                "Simulated S.M.A.R.T failure"
            });
            if (failed) {
                addEvent("Job #" + std::to_string(id) +
                         " failed (simulated S.M.A.R.T failure)");
            }
            continue;
        }
        if (progress == 100) {
            Result<void> completed = jobs_.complete(id);
            if (completed) {
                addEvent("Job #" + std::to_string(id) + " completed (simulated)");
            }
        } else {
            jobs_.updateProgress(id, progress, etaForProgress(progress));
        }
    }

    for (const JobRecord& job : jobs_.jobs()) {
        auto previous = before.find(job.id);
        if (previous != before.end() && previous->second == JobState::Queued &&
            job.state == JobState::Running) {
            jobs_.updateProgress(job.id, job.progress_percent, std::chrono::seconds{25});
            addEvent("Queued job #" + std::to_string(job.id) + " started");
        }
    }

    syncTrackedProgress();
}

void DemoSession::syncTrackedProgress() {
    auto sync = [&](DemoFeatureProgress& progress) {
        if (!progress.job_id) return;
        const JobRecord* job = jobs_.find(*progress.job_id);
        if (!job) return;
        progress.state = job->state;
        progress.progress_percent = job->progress_percent;
        progress.observed_path = job->observed_path;
    };
    sync(smart_progress_);
    sync(benchmark_progress_);
}

}  // namespace drivelab
