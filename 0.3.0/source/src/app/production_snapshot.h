#pragma once

#include "core/ownership_graph.h"
#include "core/result.h"

#include <chrono>

namespace drivelab {

// No health/ATA/benchmark fields: missing observations stay missing.
struct ProductionDrive {
    BlockDeviceObservation observation;
    ResolvedIdentity identity;
    std::optional<std::string> current_path;
    DriveStatus status = DriveStatus::Unknown;
    std::vector<OwnershipReason> reasons;
    bool proof_pending = false;
};

enum class ProductionScanPhase { Idle, Inventory, Ownership, Complete, Failed };

struct ProductionSnapshot {
    ProductionScanPhase phase = ProductionScanPhase::Idle;
    std::uint64_t generation = 0;
    std::chrono::system_clock::time_point acquired_at{};
    std::vector<ProductionDrive> drives;
    ResolvedInventorySnapshot inventory;
    OwnershipAssessment ownership;
    // Failure replaces the previous snapshot with an empty failed generation.
    std::optional<Error> scan_error;
};

}  // namespace drivelab
