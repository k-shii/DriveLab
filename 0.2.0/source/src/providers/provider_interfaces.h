#pragma once

#include "core/capability.h"
#include "core/drive.h"
#include "core/operation.h"
#include "core/result.h"

#include <vector>

namespace drivelab {

enum class HealthTestType {
    Short,
    Extended,
    Conveyance,
    RawData
};

enum class AtaInspectionType {
    Security,
    Hpa,
    Dco
};

enum class BenchmarkProfile {
    QuickRead,
    HddCharacterization,
    Random4kQd1,
    Random4kQd32,
    Latency
};

enum class SanitizeMethod {
    SecureErase,
    EnhancedSecureErase,
    Overwrite,
    OverwriteOnes,
    OverwriteRandom
};

class InventoryProvider {
public:
    virtual ~InventoryProvider() = default;

    [[nodiscard]] virtual CapabilityStatus capability() const = 0;
    virtual Result<std::vector<Drive>> scan() = 0;
    virtual Result<Drive> resolve(const DriveIdentity& expected) = 0;
};

class HealthProvider {
public:
    virtual ~HealthProvider() = default;

    [[nodiscard]] virtual CapabilityStatus capability() const = 0;
    virtual Result<OperationRequest> plan(const Drive& drive, HealthTestType test) const = 0;
};

class AtaProvider {
public:
    virtual ~AtaProvider() = default;

    [[nodiscard]] virtual CapabilityStatus capability() const = 0;
    virtual Result<OperationRequest> plan(const Drive& drive, AtaInspectionType inspection) const = 0;
};

class BenchmarkProvider {
public:
    virtual ~BenchmarkProvider() = default;

    [[nodiscard]] virtual CapabilityStatus capability() const = 0;
    virtual Result<OperationRequest> plan(const Drive& drive, BenchmarkProfile profile) const = 0;
};

class SanitizeProvider {
public:
    virtual ~SanitizeProvider() = default;

    [[nodiscard]] virtual CapabilityStatus capability() const = 0;
    virtual Result<OperationRequest> plan(const Drive& drive, SanitizeMethod method) const = 0;
};

}  // namespace drivelab
