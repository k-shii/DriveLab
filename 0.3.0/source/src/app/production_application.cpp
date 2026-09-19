#include "app/core_application.h"
#include "core/scan_control.h"
#include "core/scan_profile.h"

#include <algorithm>
#include <utility>

namespace drivelab {
namespace {
void inventoryRows(ProductionSnapshot& snapshot) {
    for (const auto& node : snapshot.inventory.nodes) {
        if (node.observation.node_type != BlockNodeType::Disk ||
            node.observation.observation_kind == BlockObservationKind::VirtualOrPseudo) continue;
        snapshot.drives.push_back({node.observation, node.identity, node.current_path,
            DriveStatus::Unknown, {}, node.observation.physical_kind == PhysicalDeviceKind::Disk});
    }
}
} // namespace

Result<std::unique_ptr<CoreApplication>> CoreApplication::createProduction(
    AppConfig config, ILogger& logger,
    std::unique_ptr<ProductionInventorySource> source) {
    const auto valid = config.validate();
    if (!valid) return Result<std::unique_ptr<CoreApplication>>::failure(valid.error());
    if (config.mode != ExecutionMode::Real || !source) {
        return Result<std::unique_ptr<CoreApplication>>::failure({
            ErrorCode::InvalidArgument, "CoreApplication",
            "Production requires real mode and a production inventory source"});
    }
    std::unique_ptr<CoreApplication> application(new CoreApplication(std::move(config), logger));
    application->production_source_ = std::move(source);
    return Result<std::unique_ptr<CoreApplication>>::success(std::move(application));
}

void CoreApplication::publishProduction(std::shared_ptr<const ProductionSnapshot> snapshot) {
    std::lock_guard lock(production_snapshot_mutex_);
    production_snapshot_ = std::move(snapshot);
}

Result<void> CoreApplication::startProductionScan() {
    std::lock_guard control(production_control_mutex_);
    if (!production_source_) return Result<void>::failure({
        ErrorCode::Unavailable, "CoreApplication", "Production discovery is unavailable in this mode"});
    if (production_running_) return Result<void>::failure({
        ErrorCode::InvalidState, "CoreApplication", "A production scan is already running"});
    if (production_worker_.joinable()) production_worker_.join();
    auto pending = std::make_shared<ProductionSnapshot>();
    pending->generation = productionSnapshotView().value()->generation + 1;
    pending->phase = ProductionScanPhase::Inventory;
    publishProduction(pending); // Immediately discard every previous verdict.
    production_running_ = true;
    const auto generation = pending->generation;
    const auto profile = active_scan_profile; // Blocking profiler keeps this alive until join.
    try {
        production_worker_ = std::jthread([this, generation, profile](std::stop_token stop) {
            ScanCancellationScope cancellation(stop);
            ScanProfileSession profiling(profile);
            ScanStage total("application.total");
            ScanStage first_inventory("application.inventory_published");
            try {
                ProductionSnapshot snapshot;
                snapshot.generation = generation;
                {
                    ScanStage acquisition("acquisition.total");
                    auto inventory = production_source_->scanInventory();
                    checkScanCancelled();
                    if (!inventory) {
                        snapshot.phase = ProductionScanPhase::Failed;
                        snapshot.scan_error = inventory.error();
                    } else {
                        snapshot.inventory = std::move(inventory.value());
                        snapshot.acquired_at = std::chrono::system_clock::now();
                        inventoryRows(snapshot);
                        snapshot.phase = ProductionScanPhase::Ownership;
                        publishProduction(std::make_shared<const ProductionSnapshot>(snapshot));
                        first_inventory.finish();
                        const bool has_disk = std::any_of(snapshot.drives.begin(), snapshot.drives.end(),
                            [](const auto& row) { return row.observation.physical_kind == PhysicalDeviceKind::Disk; });
                        auto evidence = has_disk ? production_source_->readOwnership(snapshot.inventory) : OwnershipEvidence{};
                        acquisition.finish();
                        checkScanCancelled();
                        // Keep all graph endpoints, observations and gaps. Only DISK
                        // endpoints are candidates for the unchanged C decision rules.
                        {
                            ScanStage classification("classification");
                            if (has_disk) snapshot.ownership = assessOwnership(snapshot.inventory, std::move(evidence),
                                                                 PhysicalAssessmentScope::Disks);
                        }
                        checkScanCancelled();
                        ScanStage rows("application.rows");
                        for (auto& row : snapshot.drives) {
                            row.proof_pending = false;
                            const auto key = blockOwnershipKey(row.observation.kernel_name);
                            const auto decision = std::find_if(snapshot.ownership.physical_devices.begin(),
                                snapshot.ownership.physical_devices.end(),
                                [&](const auto& value) { return value.node == key; });
                            if (decision != snapshot.ownership.physical_devices.end()) {
                                row.status = decision->status;
                                row.reasons = decision->reasons;
                            }
                        }
                        snapshot.phase = ProductionScanPhase::Complete;
                    }
                }
                checkScanCancelled();
                publishProduction(std::make_shared<const ProductionSnapshot>(std::move(snapshot)));
            } catch (const std::exception& error) {
                auto failed = std::make_shared<ProductionSnapshot>();
                failed->generation = generation;
                failed->phase = ProductionScanPhase::Failed;
                failed->scan_error = Error{ErrorCode::Unavailable, "ProductionScan", error.what()};
                publishProduction(failed);
            } catch (...) {
                auto failed = std::make_shared<ProductionSnapshot>();
                failed->generation = generation;
                failed->phase = ProductionScanPhase::Failed;
                failed->scan_error = Error{ErrorCode::Internal, "ProductionScan", "Unexpected acquisition failure"};
                publishProduction(failed);
            }
            production_running_ = false;
        });
    } catch (const std::exception& error) {
        production_running_ = false;
        auto failed = std::make_shared<ProductionSnapshot>();
        failed->generation = generation;
        failed->phase = ProductionScanPhase::Failed;
        failed->scan_error = Error{ErrorCode::Unavailable, "ProductionScan", error.what()};
        publishProduction(failed);
        return Result<void>::failure(*failed->scan_error);
    }
    return Result<void>::success();
}

void CoreApplication::waitForProductionScan() {
    std::lock_guard control(production_control_mutex_);
    if (production_worker_.joinable()) production_worker_.join();
}
void CoreApplication::cancelProductionScan() {
    std::lock_guard control(production_control_mutex_);
    production_worker_.request_stop();
    if (production_worker_.joinable()) production_worker_.join();
}
Result<void> CoreApplication::rescanProduction() {
    auto started = startProductionScan();
    if (!started) return started;
    waitForProductionScan();
    auto snapshot = productionSnapshotView();
    if (!snapshot) return Result<void>::failure(snapshot.error());
    if (snapshot.value()->scan_error) return Result<void>::failure(*snapshot.value()->scan_error);
    return Result<void>::success();
}
Result<std::shared_ptr<const ProductionSnapshot>> CoreApplication::productionSnapshotView() const {
    if (!production_source_) return Result<std::shared_ptr<const ProductionSnapshot>>::failure({
        ErrorCode::Unavailable, "CoreApplication", "Production discovery is unavailable in this mode"});
    std::lock_guard lock(production_snapshot_mutex_);
    return Result<std::shared_ptr<const ProductionSnapshot>>::success(production_snapshot_);
}
Result<ProductionSnapshot> CoreApplication::productionSnapshot() const {
    auto snapshot = productionSnapshotView();
    if (!snapshot) return Result<ProductionSnapshot>::failure(snapshot.error());
    return Result<ProductionSnapshot>::success(*snapshot.value());
}
} // namespace drivelab
