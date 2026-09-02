#pragma once

#include "core/drive.h"
#include "core/operation.h"
#include "jobs/job.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drivelab {

enum class DemoFeature {
    Overview,
    Smart,
    AtaHpa,
    Benchmark,
    Sanitize
};

enum class DemoWorkflowId {
    SmartShortTest,
    SmartExtendedTest,
    SmartConveyanceTest,
    SmartRawData,
    AtaSecurityState,
    AtaHpaInspect,
    AtaDcoInspect,
    AtaSecureErase,
    AtaEnhancedSecureErase,
    BenchmarkQuickSequential,
    BenchmarkHddZoneCharacterization,
    BenchmarkRandom4kQd1,
    BenchmarkRandom4kQd32,
    BenchmarkLatency,
    SanitizeAtaEnhancedSecureErase,
    SanitizeAtaSecureErase,
    SanitizeNwipeZeroFill,
    SanitizeNwipeOneFill,
    SanitizeNwipeRandomFill
};

enum class DemoWorkflowBehavior {
    Inspection,
    SimulatedJob
};

struct DemoContextualHelp {
    std::string what;
    std::string future_provider;
    std::string typical_use;
    std::string risk_or_limit;
};

struct DemoWorkflowDefinition {
    DemoWorkflowId id = DemoWorkflowId::SmartShortTest;
    DemoFeature feature = DemoFeature::Smart;
    std::string label;
    OperationKind operation_kind = OperationKind::HealthShortTest;
    SafetyClass safety_class = SafetyClass::ReadOnly;
    DemoWorkflowBehavior behavior = DemoWorkflowBehavior::SimulatedJob;
    DemoContextualHelp help;
    std::optional<std::chrono::minutes> estimated_duration;
    bool recommended = false;
    bool supported = true;
};

struct DemoSmartData {
    std::string health_summary;
    std::optional<int> temperature_celsius;
    std::uint64_t power_on_hours = 0;
    std::uint64_t reallocated = 0;
    std::uint64_t pending = 0;
    std::uint64_t offline_uncorrectable = 0;
};

struct DemoAtaData {
    std::string ata_security_state;
    std::string hpa_state;
    std::string security_state;
    std::optional<std::uint64_t> hpa_current_capacity_bytes;
    std::optional<std::uint64_t> hpa_native_capacity_bytes;
    std::string dco_status;
    bool secure_erase_supported = false;
    bool enhanced_secure_erase_supported = false;
    std::optional<std::chrono::minutes> secure_erase_duration;
    std::optional<std::chrono::minutes> enhanced_secure_erase_duration;
};

struct DemoBenchmarkResult {
    DemoWorkflowId workflow = DemoWorkflowId::BenchmarkQuickSequential;
    std::string result;
};

struct DemoDriveDetails {
    Drive drive;
    std::string display_capacity;
    std::string transport;
    DemoSmartData smart;
    DemoAtaData ata;
    std::vector<DemoBenchmarkResult> benchmark_results;
};

struct DemoFeatureProgress {
    DemoWorkflowId workflow = DemoWorkflowId::SmartExtendedTest;
    DriveId drive_id;
    std::string observed_path;
    JobState state = JobState::Planned;
    int progress_percent = 0;
    std::optional<JobId> job_id;
};

struct DemoEvent {
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();
    std::string message;
};

struct DemoSnapshot {
    std::vector<DemoDriveDetails> drives;
    std::vector<DemoEvent> events;
    std::vector<JobRecord> jobs;
    DemoFeatureProgress smart_progress;
    DemoFeatureProgress benchmark_progress;
};

struct DemoWorkflowCommand {
    DriveId drive_id;
    DemoWorkflowId workflow = DemoWorkflowId::SmartShortTest;
};

enum class DemoWorkflowOutcome {
    InspectionCompleted,
    JobSubmitted
};

struct DemoWorkflowResult {
    DemoWorkflowOutcome outcome = DemoWorkflowOutcome::InspectionCompleted;
    std::optional<JobId> job_id;
    std::string event_message;
};

[[nodiscard]] const std::vector<DemoWorkflowDefinition>& demoWorkflowCatalog();
[[nodiscard]] const DemoWorkflowDefinition* findDemoWorkflow(DemoWorkflowId id);
std::string demoFeatureName(DemoFeature feature);
std::string demoWorkflowSafetyLabel(const DemoWorkflowDefinition& workflow);

}  // namespace drivelab
