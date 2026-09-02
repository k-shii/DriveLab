#pragma once

#include "core/drive.h"

#include <string>

namespace drivelab {

enum class OperationKind {
    HealthShortTest,
    HealthExtendedTest,
    HealthConveyanceTest,
    HealthRawData,
    AtaSecurityInspect,
    HpaInspect,
    DcoInspect,
    BenchmarkQuickRead,
    BenchmarkHddCharacterization,
    BenchmarkRandom4kQd1,
    BenchmarkRandom4kQd32,
    BenchmarkLatency,
    SanitizeSecureErase,
    SanitizeEnhancedSecureErase,
    SanitizeOverwrite,
    SanitizeOverwriteOnes,
    SanitizeOverwriteRandom
};

enum class SafetyClass {
    StatusOnly,
    ReadOnly,
    FirmwareSelfTest,
    Destructive,
    Irreversible
};

struct ControlCapabilities {
    bool can_pause = false;
    bool can_resume = false;
    bool can_cancel = false;
};

struct OperationRequest {
    OperationKind kind = OperationKind::HealthRawData;
    std::string name;
    std::string provider;
    Drive target;
    SafetyClass safety_class = SafetyClass::StatusOnly;
    ControlCapabilities controls;
};

std::string operationKindName(OperationKind kind);
std::string safetyClassName(SafetyClass safety_class);

}  // namespace drivelab
