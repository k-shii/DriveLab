#include "providers/mock_providers.h"

#include <algorithm>
#include <utility>

namespace drivelab {
namespace {

Drive makeDrive(std::string path, std::string model, std::string serial,
                std::string wwn, std::string topology, std::uint64_t capacity,
                MediaKind media, DriveStatus status,
                std::vector<std::string> protection_reasons = {}) {
    return {
        {std::move(serial), std::move(wwn), std::move(model), std::move(topology), capacity},
        std::move(path),
        media,
        status,
        std::move(protection_reasons)
    };
}

CapabilityStatus available(CapabilityId id, std::string detail) {
    return {id, CapabilityAvailability::Available, "mock", std::move(detail)};
}

}  // namespace

MockInventoryProvider::MockInventoryProvider() : drives_(fixtureDrives()) {}

MockInventoryProvider::MockInventoryProvider(std::vector<Drive> drives)
    : drives_(std::move(drives)) {}

CapabilityStatus MockInventoryProvider::capability() const {
    return available(CapabilityId::Inventory, "Canned inventory; no device access");
}

Result<std::vector<Drive>> MockInventoryProvider::scan() {
    return Result<std::vector<Drive>>::success(drives_);
}

Result<Drive> MockInventoryProvider::resolve(const DriveIdentity& expected) {
    Result<std::string> expected_key = expected.lockKey();
    if (!expected_key) return Result<Drive>::failure(expected_key.error());

    auto match = std::find_if(drives_.begin(), drives_.end(), [&](const Drive& drive) {
        Result<std::string> candidate_key = drive.identity.lockKey();
        return candidate_key && candidate_key.value() == expected_key.value();
    });
    if (match == drives_.end()) {
        return Result<Drive>::failure({
            ErrorCode::IdentityMismatch,
            "MockInventoryProvider",
            "The expected physical drive identity is no longer present"
        });
    }
    return Result<Drive>::success(*match);
}

void MockInventoryProvider::replace(std::size_t index, Drive drive) {
    if (index < drives_.size()) drives_[index] = std::move(drive);
}

const std::vector<Drive>& MockInventoryProvider::drives() const {
    return drives_;
}

std::vector<Drive> MockInventoryProvider::fixtureDrives() {
    return {
        makeDrive("/dev/sdd", "WDC WD6400AAKS-75A7B0", "WD-WMASY1520445",
                  "0x50014ee001234567", "pci-0000:03:00.0-ata-1", 640135028736ULL,
                  MediaKind::Hdd, DriveStatus::Ready),
        makeDrive("/dev/sde", "HGST HUS724040ALA640", "PK1331PAG7JBKS",
                  "0x5000cca25abcd001", "pci-0000:04:00.0-sas-1", 4000787030016ULL,
                  MediaKind::Hdd, DriveStatus::Ready),
        makeDrive("/dev/sda", "ST2000DM001-1E6164", "Z1E6A91R",
                  "0x5000c500system01", "pci-0000:00:17.0-ata-1", 2000398934016ULL,
                  MediaKind::Hdd, DriveStatus::Protected,
                  {"System disk contains /, /boot, /home, and active swap"}),
        makeDrive("/dev/sdb", "KINGSTON SA400S37240G", "50026B7784A9D221",
                  "0x50026b7784a9d221", "pci-0000:00:17.0-ata-2", 240057409536ULL,
                  MediaKind::Ssd, DriveStatus::Protected,
                  {"Passed through as a raw disk to Proxmox VM 104"}),
        makeDrive("/dev/sdc", "WDC WD80EFZZ-68BTXN0", "VH01KJ9G",
                  "0x50014ee2abcdef01", "pci-0000:03:00.0-ata-2", 8001563222016ULL,
                  MediaKind::Hdd, DriveStatus::Protected,
                  {"Active member of imported ZFS pool tank"}),
        makeDrive("/dev/nvme0n1", "Samsung SSD 980 PRO", "S69ENF0R912345",
                  "eui.002538b912345678", "pci-0000:01:00.0-nvme-1", 2000398934016ULL,
                  MediaKind::Nvme, DriveStatus::Failing),
        makeDrive("/dev/sdf", "Seagate BarraCuda ST1000DM010", "Z9A4DEMO",
                  "0x5000c500demo0001", "pci-0000:03:00.0-ata-3", 1000204886016ULL,
                  MediaKind::Hdd, DriveStatus::Degraded)
    };
}

CapabilityStatus MockHealthProvider::capability() const {
    return available(CapabilityId::Health, "Simulated S.M.A.R.T data and self-tests");
}

Result<OperationRequest> MockHealthProvider::plan(const Drive& drive,
                                                  HealthTestType test) const {
    OperationRequest request;
    request.provider = "mock-health";
    request.target = drive;
    switch (test) {
        case HealthTestType::Short:
            request.kind = OperationKind::HealthShortTest;
            request.safety_class = SafetyClass::FirmwareSelfTest;
            request.controls.can_cancel = true;
            break;
        case HealthTestType::Extended:
            request.kind = OperationKind::HealthExtendedTest;
            request.safety_class = SafetyClass::FirmwareSelfTest;
            request.controls.can_cancel = true;
            break;
        case HealthTestType::Conveyance:
            request.kind = OperationKind::HealthConveyanceTest;
            request.safety_class = SafetyClass::FirmwareSelfTest;
            request.controls.can_cancel = true;
            break;
        case HealthTestType::RawData:
            request.kind = OperationKind::HealthRawData;
            request.safety_class = SafetyClass::StatusOnly;
            break;
    }
    request.name = operationKindName(request.kind);
    return Result<OperationRequest>::success(std::move(request));
}

CapabilityStatus MockAtaProvider::capability() const {
    return available(CapabilityId::AtaInspection, "Simulated ATA/HPA/DCO inspection");
}

Result<OperationRequest> MockAtaProvider::plan(const Drive& drive,
                                               AtaInspectionType inspection) const {
    OperationRequest request;
    request.provider = "mock-ata";
    request.target = drive;
    request.safety_class = SafetyClass::ReadOnly;
    switch (inspection) {
        case AtaInspectionType::Security: request.kind = OperationKind::AtaSecurityInspect; break;
        case AtaInspectionType::Hpa: request.kind = OperationKind::HpaInspect; break;
        case AtaInspectionType::Dco: request.kind = OperationKind::DcoInspect; break;
    }
    request.name = operationKindName(request.kind);
    return Result<OperationRequest>::success(std::move(request));
}

CapabilityStatus MockBenchmarkProvider::capability() const {
    return available(CapabilityId::Benchmark, "Simulated read-only benchmark profiles");
}

Result<OperationRequest> MockBenchmarkProvider::plan(const Drive& drive,
                                                     BenchmarkProfile profile) const {
    OperationRequest request;
    request.provider = "mock-benchmark";
    request.target = drive;
    request.safety_class = SafetyClass::ReadOnly;
    request.controls = {true, true, true};
    switch (profile) {
        case BenchmarkProfile::QuickRead:
            request.kind = OperationKind::BenchmarkQuickRead;
            break;
        case BenchmarkProfile::HddCharacterization:
            request.kind = OperationKind::BenchmarkHddCharacterization;
            break;
        case BenchmarkProfile::Random4kQd1:
            request.kind = OperationKind::BenchmarkRandom4kQd1;
            break;
        case BenchmarkProfile::Random4kQd32:
            request.kind = OperationKind::BenchmarkRandom4kQd32;
            break;
        case BenchmarkProfile::Latency:
            request.kind = OperationKind::BenchmarkLatency;
            break;
    }
    request.name = operationKindName(request.kind);
    return Result<OperationRequest>::success(std::move(request));
}

CapabilityStatus MockSanitizeProvider::capability() const {
    return available(CapabilityId::Sanitize, "Simulated destructive workflows only");
}

Result<OperationRequest> MockSanitizeProvider::plan(const Drive& drive,
                                                    SanitizeMethod method) const {
    OperationRequest request;
    request.provider = "mock-sanitize";
    request.target = drive;
    switch (method) {
        case SanitizeMethod::SecureErase:
            request.kind = OperationKind::SanitizeSecureErase;
            break;
        case SanitizeMethod::EnhancedSecureErase:
            request.kind = OperationKind::SanitizeEnhancedSecureErase;
            break;
        case SanitizeMethod::Overwrite:
            request.kind = OperationKind::SanitizeOverwrite;
            break;
        case SanitizeMethod::OverwriteOnes:
            request.kind = OperationKind::SanitizeOverwriteOnes;
            break;
        case SanitizeMethod::OverwriteRandom:
            request.kind = OperationKind::SanitizeOverwriteRandom;
            break;
    }
    request.name = operationKindName(request.kind);
    request.safety_class = (method == SanitizeMethod::SecureErase ||
                            method == SanitizeMethod::EnhancedSecureErase)
        ? SafetyClass::Irreversible
        : SafetyClass::Destructive;
    request.controls = {
        method != SanitizeMethod::SecureErase &&
            method != SanitizeMethod::EnhancedSecureErase,
        method != SanitizeMethod::SecureErase &&
            method != SanitizeMethod::EnhancedSecureErase,
        method != SanitizeMethod::SecureErase &&
            method != SanitizeMethod::EnhancedSecureErase
    };
    return Result<OperationRequest>::success(std::move(request));
}

}  // namespace drivelab
