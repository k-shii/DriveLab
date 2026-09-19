#include "core/operation.h"

namespace drivelab {

std::string operationKindName(OperationKind kind) {
    switch (kind) {
        case OperationKind::HealthShortTest: return "S.M.A.R.T Short Test";
        case OperationKind::HealthExtendedTest: return "S.M.A.R.T Extended Test";
        case OperationKind::HealthConveyanceTest: return "S.M.A.R.T Conveyance Test";
        case OperationKind::HealthRawData: return "Raw S.M.A.R.T Data";
        case OperationKind::AtaSecurityInspect: return "ATA Security Inspect";
        case OperationKind::HpaInspect: return "HPA Inspect";
        case OperationKind::DcoInspect: return "DCO Inspect";
        case OperationKind::BenchmarkQuickRead: return "Quick Read";
        case OperationKind::BenchmarkHddCharacterization: return "HDD Characterization";
        case OperationKind::BenchmarkRandom4kQd1: return "Random 4K QD1";
        case OperationKind::BenchmarkRandom4kQd32: return "Random 4K QD32";
        case OperationKind::BenchmarkLatency: return "Latency / ioping";
        case OperationKind::SanitizeSecureErase: return "Secure Erase";
        case OperationKind::SanitizeEnhancedSecureErase: return "Enhanced Secure Erase";
        case OperationKind::SanitizeOverwrite: return "nwipe Zero Fill";
        case OperationKind::SanitizeOverwriteOnes: return "nwipe One Fill";
        case OperationKind::SanitizeOverwriteRandom: return "nwipe Random Fill";
    }
    return "Unknown operation";
}

std::string safetyClassName(SafetyClass safety_class) {
    switch (safety_class) {
        case SafetyClass::StatusOnly: return "STATUS ONLY";
        case SafetyClass::ReadOnly: return "READ-ONLY";
        case SafetyClass::FirmwareSelfTest: return "FIRMWARE SELF-TEST";
        case SafetyClass::Destructive: return "DESTRUCTIVE";
        case SafetyClass::Irreversible: return "IRREVERSIBLE";
    }
    return "UNKNOWN";
}

}  // namespace drivelab
