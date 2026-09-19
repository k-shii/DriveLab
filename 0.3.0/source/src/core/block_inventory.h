#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drivelab {

enum class BlockNodeType {
    Unknown,
    Disk,
    Partition
};

// Physical media/capability, independent of transport, identity and C safety.
enum class PhysicalDeviceKind { Disk, Optical, Floppy, UnknownPhysical };

inline const char* physicalDeviceKindName(PhysicalDeviceKind kind) {
    switch (kind) {
        case PhysicalDeviceKind::Disk: return "DISK";
        case PhysicalDeviceKind::Optical: return "OPTICAL";
        case PhysicalDeviceKind::Floppy: return "FLOPPY";
        default: return "UNKNOWN_PHYSICAL";
    }
}
struct DeviceKindObservation {
    std::string source;
    std::string value;
    friend bool operator==(const DeviceKindObservation&, const DeviceKindObservation&) = default;
};

enum class BlockObservationKind {
    Unknown,
    VirtualOrPseudo
};

struct BlockDeviceNumber {
    std::uint32_t major_number = 0;
    std::uint32_t minor_number = 0;

    friend bool operator==(const BlockDeviceNumber&, const BlockDeviceNumber&) = default;
};

enum class BlockInventoryIssueCode {
    MissingRequiredField,
    MalformedAttribute,
    CapacityOverflow,
    DuplicateRecord,
    DeviceDisappeared,
    SourceReadFailure,
    ConflictingAttribute,
    InvalidRelationship
};

struct BlockInventoryIssue {
    BlockInventoryIssueCode code = BlockInventoryIssueCode::SourceReadFailure;
    std::optional<std::string> kernel_name;
    std::string field;
    std::string message;
    std::optional<int> native_error = std::nullopt;

    friend bool operator==(const BlockInventoryIssue&, const BlockInventoryIssue&) = default;
};

struct BlockModelObservation {
    std::string source;
    std::string value;
    friend bool operator==(const BlockModelObservation&, const BlockModelObservation&) = default;
};

// Observations only: no stable identity, ownership, or safety decision.
// A disengaged field without an issue is absent; failures/conflicts carry issues.
struct BlockDeviceObservation {
    std::string kernel_name;
    std::optional<std::string> current_path;
    std::optional<std::string> sysfs_path;
    BlockNodeType node_type = BlockNodeType::Unknown;
    std::optional<BlockDeviceNumber> device_number;
    std::optional<std::uint64_t> capacity_bytes;
    std::optional<std::string> vendor;
    std::optional<std::string> model;
    std::optional<std::string> serial;
    std::optional<std::string> wwn;
    std::optional<std::string> nvme_eui;
    std::optional<std::string> nvme_nguid;
    std::optional<bool> rotational;
    std::optional<bool> removable;
    std::optional<bool> read_only;
    std::optional<std::string> transport;
    std::optional<std::string> parent_kernel_name;
    std::vector<std::string> child_kernel_names;
    std::optional<std::uint32_t> partition_number;
    BlockObservationKind observation_kind = BlockObservationKind::Unknown;
    std::vector<BlockInventoryIssue> issues;

    // Original model bytes and acquisition provenance, including equivalent forms.
    std::vector<BlockModelObservation> model_observations;
    PhysicalDeviceKind physical_kind = PhysicalDeviceKind::UnknownPhysical;
    std::vector<DeviceKindObservation> device_kind_observations;

    friend bool operator==(const BlockDeviceObservation&,
                           const BlockDeviceObservation&) = default;
};

struct BlockInventorySnapshot {
    std::vector<BlockDeviceObservation> devices;
    std::vector<BlockInventoryIssue> issues;

    friend bool operator==(const BlockInventorySnapshot&,
                           const BlockInventorySnapshot&) = default;
};

}  // namespace drivelab
