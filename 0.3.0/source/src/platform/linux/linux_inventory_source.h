#pragma once

#include "core/block_inventory.h"
#include "core/result.h"

#include <optional>
#include <string>
#include <vector>

namespace drivelab {

struct LinuxBlockSourceRecord {
    std::string kernel_name;
    std::optional<std::string> current_path;
    std::optional<std::string> sysfs_path;
    std::optional<std::string> node_type;
    std::optional<std::string> major_number;
    std::optional<std::string> minor_number;
    std::optional<std::string> capacity_sectors_512;
    std::optional<std::string> vendor;
    std::optional<std::string> model;
    std::optional<std::string> serial;
    std::optional<std::string> wwn;
    std::optional<std::string> nvme_eui;
    std::optional<std::string> nvme_nguid;
    std::optional<std::string> rotational;
    std::optional<std::string> removable;
    std::optional<std::string> read_only;
    std::optional<std::string> transport;
    std::optional<std::string> parent_kernel_name;
    std::optional<std::string> partition_number;
    std::vector<BlockModelObservation> model_observations;
    std::vector<DeviceKindObservation> device_kind_observations;
    BlockObservationKind observation_kind = BlockObservationKind::Unknown;
    std::vector<BlockInventoryIssue> issues;
};

struct LinuxInventorySourceSnapshot {
    std::vector<LinuxBlockSourceRecord> records;
    std::vector<BlockInventoryIssue> issues;
};

class LinuxInventorySource {
public:
    virtual ~LinuxInventorySource() = default;

    virtual Result<LinuxInventorySourceSnapshot> scan() = 0;
};

class UdevSysfsInventorySource final : public LinuxInventorySource {
public:
    Result<LinuxInventorySourceSnapshot> scan() override;
};

}  // namespace drivelab
