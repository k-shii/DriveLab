#include "core/demo_model.h"

namespace drivelab {

const std::vector<DemoWorkflowDefinition>& demoWorkflowCatalog() {
    static const std::vector<DemoWorkflowDefinition> workflows = {
        {DemoWorkflowId::SmartShortTest, DemoFeature::Smart, "Short Test",
         OperationKind::HealthShortTest, SafetyClass::FirmwareSelfTest,
         DemoWorkflowBehavior::SimulatedJob,
         {"Firmware electrical/mechanical self-check without scanning all media.",
          "smartctl self-test adapter", "Fast first-pass health triage.",
          "May briefly affect latency; support varies by device."},
         std::nullopt},
        {DemoWorkflowId::SmartExtendedTest, DemoFeature::Smart, "Extended Test",
         OperationKind::HealthExtendedTest, SafetyClass::FirmwareSelfTest,
         DemoWorkflowBehavior::SimulatedJob,
         {"Drive firmware self-test across the media; it may take hours.",
          "smartctl self-test adapter", "Deep health validation after quick checks.",
          "Creates sustained device workload and can be aborted only if supported."},
         std::nullopt},
        {DemoWorkflowId::SmartConveyanceTest, DemoFeature::Smart, "Conveyance Test",
         OperationKind::HealthConveyanceTest, SafetyClass::FirmwareSelfTest,
         DemoWorkflowBehavior::SimulatedJob,
         {"Short firmware test intended to detect shipping damage.",
          "smartctl self-test adapter", "Recently transported ATA drives.",
          "Often unsupported on SSD, SAS and NVMe devices."},
         std::nullopt},
        {DemoWorkflowId::SmartRawData, DemoFeature::Smart, "Raw S.M.A.R.T Data",
         OperationKind::HealthRawData, SafetyClass::StatusOnly,
         DemoWorkflowBehavior::Inspection,
         {"Displays unprocessed attributes, logs and device counters.",
          "smartctl health adapter", "Vendor-specific diagnosis and support cases.",
          "Attribute meanings and thresholds vary by model and firmware."},
         std::nullopt},

        {DemoWorkflowId::AtaSecurityState, DemoFeature::AtaHpa, "Security State",
         OperationKind::AtaSecurityInspect, SafetyClass::ReadOnly,
         DemoWorkflowBehavior::Inspection,
         {"Inspects ATA security enabled, locked and frozen flags.",
          "hdparm security adapter", "Determine whether firmware erase can be authorized.",
          "USB bridges may hide or misreport ATA security state."},
         std::nullopt},
        {DemoWorkflowId::AtaHpaInspect, DemoFeature::AtaHpa, "HPA Inspect",
         OperationKind::HpaInspect, SafetyClass::ReadOnly,
         DemoWorkflowBehavior::Inspection,
         {"Compares current visible capacity with the native maximum.",
          "hdparm HPA adapter", "Detect host-protected hidden sectors.",
          "Capacity changes are intentionally unavailable in this baseline."},
         std::nullopt},
        {DemoWorkflowId::AtaDcoInspect, DemoFeature::AtaHpa, "DCO Inspect",
         OperationKind::DcoInspect, SafetyClass::ReadOnly,
         DemoWorkflowBehavior::Inspection,
         {"Reads Device Configuration Overlay limits and capabilities.",
          "hdparm DCO identify adapter", "Explain capacity or feature restrictions.",
          "DCO modification is out of scope; identify only."},
         std::nullopt},
        {DemoWorkflowId::AtaSecureErase, DemoFeature::AtaHpa, "Secure Erase",
         OperationKind::SanitizeSecureErase, SafetyClass::Irreversible,
         DemoWorkflowBehavior::SimulatedJob,
         {"Requests the drive firmware's standard security erase.",
          "hdparm erase adapter", "Firmware-native sanitization of ATA media.",
          "Non-cancellable after acceptance; power loss may leave security enabled."},
         std::chrono::minutes{102}},
        {DemoWorkflowId::AtaEnhancedSecureErase, DemoFeature::AtaHpa,
         "Enhanced Secure Erase", OperationKind::SanitizeEnhancedSecureErase,
         SafetyClass::Irreversible, DemoWorkflowBehavior::SimulatedJob,
         {"Requests the firmware's enhanced erase procedure.",
          "hdparm enhanced-erase adapter",
          "Preferred when supported and duration is acceptable.",
          "Support and completion reporting are firmware-dependent."},
         std::chrono::minutes{78}},

        {DemoWorkflowId::BenchmarkQuickSequential, DemoFeature::Benchmark,
         "Quick Sequential", OperationKind::BenchmarkQuickRead,
         SafetyClass::ReadOnly, DemoWorkflowBehavior::SimulatedJob,
         {"Samples sequential read throughput over a small safe range.",
          "fio benchmark adapter", "Fast throughput sanity check.",
          "A short sample does not describe whole-drive performance."},
         std::nullopt},
        {DemoWorkflowId::BenchmarkHddZoneCharacterization, DemoFeature::Benchmark,
         "HDD Zone Characterization", OperationKind::BenchmarkHddCharacterization,
         SafetyClass::ReadOnly, DemoWorkflowBehavior::SimulatedJob,
         {"Reads outer, middle and inner zones separately.",
          "fio benchmark adapter", "Reveal rotational-media zone performance falloff.",
          "Longer runtime and sustained workload; HDD-oriented."},
         std::nullopt},
        {DemoWorkflowId::BenchmarkRandom4kQd1, DemoFeature::Benchmark,
         "Random 4K QD1", OperationKind::BenchmarkRandom4kQd1,
         SafetyClass::ReadOnly, DemoWorkflowBehavior::SimulatedJob,
         {"Measures single-queue small-block random reads.",
          "fio benchmark adapter", "Estimate desktop-like responsiveness.",
          "Results are sensitive to cache and competing host I/O."},
         std::nullopt},
        {DemoWorkflowId::BenchmarkRandom4kQd32, DemoFeature::Benchmark,
         "Random 4K QD32", OperationKind::BenchmarkRandom4kQd32,
         SafetyClass::ReadOnly, DemoWorkflowBehavior::SimulatedJob,
         {"Measures deep-queue small-block random reads.",
          "fio benchmark adapter", "Evaluate maximum queued random-read capability.",
          "Not representative of most interactive workloads."},
         std::nullopt},
        {DemoWorkflowId::BenchmarkLatency, DemoFeature::Benchmark,
         "Latency / ioping", OperationKind::BenchmarkLatency,
         SafetyClass::ReadOnly, DemoWorkflowBehavior::SimulatedJob,
         {"Samples request latency and distribution over time.",
          "ioping latency adapter", "Find stalls, jitter and tail-latency problems.",
          "Host contention can dominate device latency."},
         std::nullopt},

        {DemoWorkflowId::SanitizeAtaEnhancedSecureErase, DemoFeature::Sanitize,
         "ATA Enhanced Secure Erase", OperationKind::SanitizeEnhancedSecureErase,
         SafetyClass::Irreversible, DemoWorkflowBehavior::SimulatedJob,
         {"Firmware performs its enhanced media sanitization routine.",
          "hdparm enhanced-erase adapter", "Preferred ATA method when supported.",
          "Non-cancellable after acceptance; estimated 1h18m on this demo drive."},
         std::chrono::minutes{78}, true},
        {DemoWorkflowId::SanitizeAtaSecureErase, DemoFeature::Sanitize,
         "ATA Secure Erase", OperationKind::SanitizeSecureErase,
         SafetyClass::Irreversible, DemoWorkflowBehavior::SimulatedJob,
         {"Firmware performs the standard ATA security erase.",
          "hdparm erase adapter", "Native erase when enhanced mode is unavailable.",
          "Non-cancellable after acceptance; estimated 1h42m."},
         std::chrono::minutes{102}},
        {DemoWorkflowId::SanitizeNwipeZeroFill, DemoFeature::Sanitize,
         "nwipe Zero Fill", OperationKind::SanitizeOverwrite,
         SafetyClass::Destructive, DemoWorkflowBehavior::SimulatedJob,
         {"Writes zero bytes across every addressable sector.",
          "nwipe sanitize adapter", "Simple overwrite with easy post-write verification.",
          "Slow on large disks and may not cover remapped sectors."},
         std::nullopt},
        {DemoWorkflowId::SanitizeNwipeOneFill, DemoFeature::Sanitize,
         "nwipe One Fill", OperationKind::SanitizeOverwriteOnes,
         SafetyClass::Destructive, DemoWorkflowBehavior::SimulatedJob,
         {"Writes 0xFF bytes across every addressable sector.",
          "nwipe sanitize adapter", "Alternative deterministic overwrite pattern.",
          "No security advantage over zero fill for modern drives."},
         std::nullopt},
        {DemoWorkflowId::SanitizeNwipeRandomFill, DemoFeature::Sanitize,
         "nwipe Random Fill", OperationKind::SanitizeOverwriteRandom,
         SafetyClass::Destructive, DemoWorkflowBehavior::SimulatedJob,
         {"Writes pseudorandom data across every addressable sector.",
          "nwipe sanitize adapter",
          "Policy-driven overwrite where random patterns are required.",
          "Slower to generate/verify and may not cover remapped sectors."},
         std::nullopt}
    };
    return workflows;
}

const DemoWorkflowDefinition* findDemoWorkflow(DemoWorkflowId id) {
    for (const DemoWorkflowDefinition& workflow : demoWorkflowCatalog()) {
        if (workflow.id == id) return &workflow;
    }
    return nullptr;
}

std::string demoFeatureName(DemoFeature feature) {
    switch (feature) {
        case DemoFeature::Overview: return "Overview";
        case DemoFeature::Smart: return "S.M.A.R.T";
        case DemoFeature::AtaHpa: return "ATA / HPA";
        case DemoFeature::Benchmark: return "Benchmark";
        case DemoFeature::Sanitize: return "Sanitize";
    }
    return "Unknown";
}

std::string demoWorkflowSafetyLabel(const DemoWorkflowDefinition& workflow) {
    if (workflow.safety_class == SafetyClass::Destructive ||
        workflow.safety_class == SafetyClass::Irreversible) {
        return "DESTRUCTIVE";
    }
    return "READ-ONLY";
}

}  // namespace drivelab
