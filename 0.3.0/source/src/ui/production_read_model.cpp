#include "ui/production_read_model.h"

#include "app/core_application.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace drivelab {

std::string productionStatusLabel(DriveStatus status) {
    switch (status) {
        case DriveStatus::Ready: return "READY";
        case DriveStatus::Busy: return "BUSY";
        case DriveStatus::Protected: return "RESTRICTED";
        default: return "CAUTION";
    }
}

std::string productionStatusLabel(const ProductionDrive& drive) {
    if (drive.observation.physical_kind != PhysicalDeviceKind::Disk)
        return physicalDeviceKindName(drive.observation.physical_kind);
    return drive.proof_pending ? "SCANNING" : productionStatusLabel(drive.status);
}

std::string presentationText(const std::string& text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (const unsigned char c : text) {
        if (c >= 32 && c <= 126) output += static_cast<char>(c);
        else {
            output += "\\x";
            output += hex[c >> 4];
            output += hex[c & 15];
        }
    }
    return output;
}

std::string productionCapacity(const std::optional<std::uint64_t>& bytes) {
    if (!bytes) return "Unknown size";
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << static_cast<double>(*bytes) / (1024.0 * 1024.0 * 1024.0) << " GiB";
    return out.str();
}

std::string productionMedia(const BlockDeviceObservation& observation) {
    if (observation.physical_kind != PhysicalDeviceKind::Disk)
        return physicalDeviceKindName(observation.physical_kind);
    if (observation.transport == "nvme") return "NVMe";
    if (observation.rotational == true) return "HDD";
    if (observation.rotational == false) return "SSD";
    return "Unknown media";
}

namespace {
std::string reasonLabel(OwnershipReasonCode code) {
    using C = OwnershipReasonCode;
    switch (code) {
        case C::ChecksComplete: return "No ownership or use found within the completed checks";
        case C::IdentityUnavailable: return "Stable identity could not be verified";
        case C::PathUnavailable: return "Current device path could not be verified";
        case C::ReadOnly: return "Device is read-only";
        case C::Mount: return "Mounted filesystem";
        case C::SystemRoot: return "Contains the running system root";
        case C::BootFilesystem: return "Contains boot storage";
        case C::HomeFilesystem: return "Contains home storage";
        case C::ActiveSwap: return "Active swap";
        case C::Holder: return "Held by another storage device";
        case C::LvmMember: return "LVM membership";
        case C::RaidMember: return "RAID membership";
        case C::ZfsMember: return "ZFS membership";
        case C::ConfiguredStorage: return "Configured host storage";
        case C::GuestAssignment: return "Assigned to a guest";
        case C::OpenBlockHandle: return "A process holds the block device open";
        case C::OpenFileHandle: return "A process holds files on this storage";
        case C::MissingSource: return "An ownership source is unavailable";
        case C::MissingRelationship: return "Storage backing relationship is missing";
        case C::AsymmetricRelationship: return "Storage backing links disagree";
        case C::DuplicateNode: return "Duplicate storage observations";
        case C::Cycle: return "Storage backing relationship contains a cycle";
        case C::UnsupportedTopology: return "Storage topology is unsupported";
        case C::ConflictingEvidence: return "Observations are missing or disagree";
        case C::SnapshotChanged: return "Storage changed during acquisition";
        case C::IncompleteCoverage: return "Ownership or use could not be fully verified";
        case C::SignatureUnavailable: return "Fresh signature or membership evidence is incomplete";
        case C::NamespaceScope: return "Shared physical controller ownership is unverified";
    }
    return "Unrecognized evidence";
}

bool sameSelection(const ProductionDrive& before, const ProductionDrive& after) {
    // Paths and topology are observations. Stable ID plus secondary identity
    // facts must still agree; even a lost fact invalidates selection.
    return before.identity.state == ResolutionState::Resolved &&
           after.identity.state == ResolutionState::Resolved &&
           before.identity.id && before.identity.id == after.identity.id &&
           before.identity.scope == after.identity.scope &&
           before.observation.physical_kind == after.observation.physical_kind &&
           before.observation.serial == after.observation.serial &&
           before.observation.model == after.observation.model &&
           before.observation.capacity_bytes == after.observation.capacity_bytes &&
           before.observation.wwn == after.observation.wwn &&
           before.observation.nvme_eui == after.observation.nvme_eui &&
           before.observation.nvme_nguid == after.observation.nvme_nguid;
}
}  // namespace

std::vector<std::string> productionReasonSummary(const ProductionDrive& drive) {
    if (drive.observation.physical_kind != PhysicalDeviceKind::Disk)
        return {"Disk safety workflow unavailable for this device kind.", "No disk readiness assessment or operations."};
    if (drive.proof_pending) return {"Ownership and safety proof is incomplete; readiness is unverified."};
    std::map<OwnershipReasonCode, std::size_t> counts;
    for (const auto& reason : drive.reasons) ++counts[reason.code];
    std::vector<std::string> lines;
    for (const auto& [code, count] : counts) {
        auto line = reasonLabel(code);
        if (count > 1) line += " (" + std::to_string(count) + " evidence records)";
        lines.push_back(std::move(line));
    }
    return lines;
}

Result<void> ProductionReadModel::rescan() {
    auto result = application_.rescanProduction();
    auto refreshed = refresh();
    return !result ? result : refreshed;
}

Result<void> ProductionReadModel::startScan() {
    auto result = application_.startProductionScan();
    auto refreshed = refresh();
    return !result ? result : refreshed;
}

Result<void> ProductionReadModel::refresh() {
    auto next = application_.productionSnapshotView();
    if (!next) {
        snapshot_ = std::make_shared<const ProductionSnapshot>();
        selection_anchor_.reset();
        selected_.reset();
        selection_notice_ = "Selection cleared: production snapshot unavailable.";
        return Result<void>::failure(next.error());
    }
    if (next.value() == snapshot_) return Result<void>::success();
    if (next.value()->generation != snapshot_->generation) {
        if (const auto* before = selected())
            selection_anchor_ = ProductionDrive{before->observation, before->identity, before->current_path,
                                               DriveStatus::Unknown, {}, false};
        selected_.reset();
    }
    if (next.value()->phase != ProductionScanPhase::Inventory && selection_anchor_) {
        const auto* before = &*selection_anchor_;
        std::optional<std::size_t> retained;
        if (before && !next.value()->scan_error) {
            for (std::size_t i = 0; i < next.value()->drives.size(); ++i) {
                if (!sameSelection(*before, next.value()->drives[i])) continue;
                if (retained) { retained.reset(); break; } // ambiguous match
                retained = i;
            }
        }
        if (before && !retained)
            selection_notice_ = "Selection cleared: drive disappeared, identity changed, or scan failed.";
        selected_ = retained;
        selection_anchor_.reset();
    }
    if (next.value()->scan_error) {
        if (selected_) selection_notice_ = "Selection cleared: scan failed.";
        selected_.reset();
        selection_anchor_.reset();
    }
    snapshot_ = std::move(next.value());
    return Result<void>::success();
}

bool ProductionReadModel::select(std::size_t index) {
    if (index >= snapshot_->drives.size() || snapshot_->scan_error) return false;
    selected_ = index;
    selection_anchor_.reset();
    selection_notice_.clear();
    return true;
}

const ProductionDrive* ProductionReadModel::selected() const {
    return selected_ && *selected_ < snapshot_->drives.size() ? &snapshot_->drives[*selected_] : nullptr;
}

std::vector<std::string> ProductionReadModel::overview(bool detailed) const {
    const auto* drive = selected();
    if (!drive) return {selection_notice_.empty() ? "Select a drive to inspect its identity and safety reasons." : selection_notice_};
    std::vector<std::string> lines{productionStatusLabel(*drive)};
    auto reasons = productionReasonSummary(*drive);
    lines.insert(lines.end(), reasons.begin(), reasons.end());
    lines.push_back("");
    lines.push_back("Current path: " + presentationText(drive->current_path.value_or("Unavailable")));
    lines.push_back("Model: " + presentationText(drive->observation.model.value_or("Unavailable")));
    lines.push_back("Serial: " + presentationText(drive->observation.serial.value_or("Unavailable")));
    lines.push_back(std::string("Device kind: ") + physicalDeviceKindName(drive->observation.physical_kind));
    lines.push_back("Transport: " + presentationText(drive->observation.transport.value_or("Unavailable")));
    lines.push_back(std::string(drive->observation.physical_kind == PhysicalDeviceKind::Optical ? "Reported media size: " : "Capacity: ") + productionCapacity(drive->observation.capacity_bytes));
    if (drive->observation.capacity_bytes)
        lines.push_back("Raw capacity: " + std::to_string(*drive->observation.capacity_bytes) + " bytes");
    lines.push_back("Stable identity: " + presentationText(drive->identity.id ? drive->identity.id->value : "Unresolved"));
    lines.push_back(drive->identity.scope == IdentityScope::NvmeNamespace
        ? "Identity scope: NVMe namespace" : "Identity scope: whole device");
    if (drive->observation.physical_kind != PhysicalDeviceKind::Disk) return lines;
    const auto key = blockOwnershipKey(drive->observation.kernel_name);
    const auto fresh = snapshot_->ownership.fresh_signatures.find(key);
    if (fresh != snapshot_->ownership.fresh_signatures.end()) {
        lines.push_back("Fresh signature: " + freshSignatureStateName(fresh->second.state));
        if (!fresh->second.type.empty()) lines.push_back("Signature type: " + presentationText(fresh->second.type));
        if (!fresh->second.partition_table.empty())
            lines.push_back("Partition table: " + presentationText(fresh->second.partition_table));
        else if (freshSignatureComplete(fresh->second))
            lines.push_back("Partition table: none reported");
    } else lines.push_back("Fresh signature: unavailable");
    lines.push_back("Health: not assessed (diagnostics are deferred to 0.4).");
    lines.push_back("READY describes ownership and use, not disk health.");
    lines.push_back("Snapshot only. Rescan after changes; operations are unavailable.");
    lines.push_back("");
    lines.push_back("Provider coverage");
    for (const auto& [source, complete] : snapshot_->ownership.coverage)
        lines.push_back(presentationText(source) + ": " + (complete ? "complete" : "incomplete / unavailable"));
    lines.push_back("Scoped provider gaps are interpreted by Core for each drive.");
    if (detailed) {
        lines.push_back("");
        lines.push_back("Classification evidence");
        for (const auto& reason : drive->reasons) {
            lines.push_back(ownershipReasonName(reason.code) + " | " +
                presentationText(reason.node) + " | " + presentationText(reason.source));
            lines.push_back(presentationText(reason.detail));
        }
    }
    return lines;
}

}  // namespace drivelab
