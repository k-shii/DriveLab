#include "platform/linux/linux_inventory_provider.h"
#include "core/scan_profile.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace drivelab {
namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char character) {
        return character != ' ' && character != '\t' && character != '\n' &&
               character != '\r' && character != '\f' && character != '\v';
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void addIssue(std::vector<BlockInventoryIssue>& issues,
              BlockInventoryIssueCode code,
              std::optional<std::string> kernel_name,
              std::string field,
              std::string message) {
    issues.push_back({code, std::move(kernel_name), std::move(field), std::move(message)});
}

bool issueLess(const BlockInventoryIssue& left, const BlockInventoryIssue& right) {
    if (left.code != right.code) {
        return static_cast<int>(left.code) < static_cast<int>(right.code);
    }
    return std::tie(left.kernel_name, left.field, left.message, left.native_error) <
           std::tie(right.kernel_name, right.field, right.message, right.native_error);
}

void normalizeIssues(std::vector<BlockInventoryIssue>& issues) {
    std::sort(issues.begin(), issues.end(), issueLess);
    issues.erase(std::unique(issues.begin(), issues.end()), issues.end());
}

bool unreliable(const std::vector<BlockInventoryIssue>& issues, const std::string& field) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
        return issue.field == field &&
            (issue.code == BlockInventoryIssueCode::SourceReadFailure ||
             issue.code == BlockInventoryIssueCode::DeviceDisappeared ||
             issue.code == BlockInventoryIssueCode::ConflictingAttribute ||
             issue.code == BlockInventoryIssueCode::MalformedAttribute ||
             issue.code == BlockInventoryIssueCode::CapacityOverflow);
    });
}

std::optional<std::string> parseText(const std::optional<std::string>& raw,
                                     const std::string& field,
                                     const std::string& kernel_name,
                                     std::vector<BlockInventoryIssue>& issues) {
    if (!raw || unreliable(issues, field)) return std::nullopt;

    std::string value = trim(*raw);
    if (!value.empty() && std::none_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 32 || c == 127;
        })) return value;

    addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
             field, "The observed text is empty or contains control characters");
    return std::nullopt;
}

template <typename Unsigned>
std::optional<Unsigned> parseUnsigned(const std::optional<std::string>& raw,
                                      const std::string& field,
                                      const std::string& kernel_name,
                                      std::vector<BlockInventoryIssue>& issues) {
    if (!raw || unreliable(issues, field)) return std::nullopt;

    const std::string value = trim(*raw);
    Unsigned parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || conversion.ec != std::errc() ||
        conversion.ptr != value.data() + value.size()) {
        addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
                 field, "The observed value is not an unsigned decimal integer");
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parseBoolean(const std::optional<std::string>& raw,
                                 const std::string& field,
                                 const std::string& kernel_name,
                                 std::vector<BlockInventoryIssue>& issues) {
    if (!raw || unreliable(issues, field)) return std::nullopt;

    const std::string value = trim(*raw);
    if (value == "0") return false;
    if (value == "1") return true;

    addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
             field, "The observed sysfs boolean is neither 0 nor 1");
    return std::nullopt;
}

BlockNodeType parseNodeType(const std::optional<std::string>& raw,
                            const std::string& kernel_name,
                            std::vector<BlockInventoryIssue>& issues) {
    if (unreliable(issues, "node_type")) return BlockNodeType::Unknown;
    if (!raw) {
        addIssue(issues, BlockInventoryIssueCode::MissingRequiredField, kernel_name,
                 "node_type", "The source did not report a block node type");
        return BlockNodeType::Unknown;
    }

    const std::string value = trim(*raw);
    if (value == "disk") return BlockNodeType::Disk;
    if (value == "partition") return BlockNodeType::Partition;

    addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
             "node_type", "The source reported an unsupported block node type");
    return BlockNodeType::Unknown;
}

std::optional<BlockDeviceNumber> parseDeviceNumber(
    const LinuxBlockSourceRecord& record,
    const std::string& kernel_name,
    std::vector<BlockInventoryIssue>& issues) {
    if (unreliable(issues, "device_number")) return std::nullopt;
    if (!record.major_number || !record.minor_number) {
        addIssue(issues, BlockInventoryIssueCode::MissingRequiredField, kernel_name,
                 "device_number", "The source did not report both major and minor numbers");
        return std::nullopt;
    }

    const std::optional<std::uint32_t> major_number = parseUnsigned<std::uint32_t>(
        record.major_number, "major_number", kernel_name, issues);
    const std::optional<std::uint32_t> minor_number = parseUnsigned<std::uint32_t>(
        record.minor_number, "minor_number", kernel_name, issues);
    if (!major_number || !minor_number) return std::nullopt;
    return BlockDeviceNumber{*major_number, *minor_number};
}

std::optional<std::uint64_t> parseCapacity(
    const std::optional<std::string>& raw,
    const std::string& kernel_name,
    std::vector<BlockInventoryIssue>& issues) {
    if (!raw || unreliable(issues, "capacity_sectors_512")) return std::nullopt;

    const std::string value = trim(*raw);
    std::uint64_t sectors = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), sectors);
    if (conversion.ec == std::errc::result_out_of_range &&
        conversion.ptr == value.data() + value.size()) {
        addIssue(issues, BlockInventoryIssueCode::CapacityOverflow, kernel_name,
                 "capacity_sectors_512", "The observed sector count exceeds uint64 range");
        return std::nullopt;
    }
    if (value.empty() || conversion.ec != std::errc() ||
        conversion.ptr != value.data() + value.size()) {
        addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
                 "capacity_sectors_512",
                 "The observed sector count is not an unsigned decimal integer");
        return std::nullopt;
    }
    if (sectors > std::numeric_limits<std::uint64_t>::max() / 512ULL) {
        addIssue(issues, BlockInventoryIssueCode::CapacityOverflow, kernel_name,
                 "capacity_sectors_512",
                 "Converting 512-byte sectors to bytes would overflow uint64");
        return std::nullopt;
    }
    return sectors * 512ULL;
}

std::optional<std::string> parsePath(const std::optional<std::string>& raw,
                                     const std::string& prefix,
                                     const std::string& field,
                                     const std::string& kernel_name,
                                     std::vector<BlockInventoryIssue>& issues) {
    auto value = parseText(raw, field, kernel_name, issues);
    if (!value) return std::nullopt;
    const std::string terminated = *value + "/";
    if (value->starts_with(prefix) && value->size() > prefix.size() &&
        value->back() != '/' && terminated.find("/../") == std::string::npos &&
        terminated.find("/./") == std::string::npos &&
        value->find("//") == std::string::npos) return value;
    addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
             field, "The observed path is not an absolute path under " + prefix);
    return std::nullopt;
}

std::optional<std::string> parseNvmeIdentifier(const std::optional<std::string>& raw,
                                              std::size_t length,
                                              const std::string& field,
                                              const std::string& kernel_name,
                                              std::vector<BlockInventoryIssue>& issues) {
    auto value = parseText(raw, field, kernel_name, issues);
    if (!value) return std::nullopt;
    // Linux emits EUI as eight space-separated octets and NGUID in UUID
    // notation. Accept those and compact hex fixtures, preserving source spelling.
    std::string digits;
    bool layout_valid = value->size() == length;
    if (length == 16 && value->size() == 23) {
        layout_valid = true;
        for (std::size_t i = 0; i < value->size(); ++i) {
            if (i % 3 == 2) layout_valid = layout_valid && (*value)[i] == ' ';
            else digits += (*value)[i];
        }
    } else if (length == 32 && value->size() == 36) {
        layout_valid = true;
        for (std::size_t i = 0; i < value->size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                layout_valid = layout_valid && (*value)[i] == '-';
            } else digits += (*value)[i];
        }
    } else {
        digits = *value;
    }
    if (layout_valid && digits.size() == length &&
        digits.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos &&
        digits.find_first_not_of('0') != std::string::npos) return value;
    addIssue(issues, BlockInventoryIssueCode::MalformedAttribute, kernel_name,
             field, "The observed NVMe identifier is not a nonzero EUI/NGUID in a supported sysfs format");
    return std::nullopt;
}

PhysicalDeviceKind physicalKind(const LinuxBlockSourceRecord& record,
                                const std::string& name,
                                std::vector<BlockInventoryIssue>& issues) {
    using Kind = PhysicalDeviceKind;
    bool disk = false, optical = false, floppy = false, nvme = false, unknown = false;
    for (const auto& fact : record.device_kind_observations) {
        const auto value = trim(fact.value);
        if (fact.source == "sysfs:scsi/type") {
            const auto type = parseUnsigned<unsigned>(value, "physical_kind", name, issues);
            if (!type) unknown = true;
            else if (*type == 0 || *type == 14 || *type == 20) disk = true;
            else if (*type == 4 || *type == 5 || *type == 7 || *type == 15) optical = true;
            else unknown = true;
        } else if (fact.source == "udev:ID_TYPE" || fact.source == "udev:ID_USB_TYPE") {
            if (value == "disk") disk = true;
            else if (value == "cd" || value == "optical") optical = true;
            else if (value == "floppy") floppy = true;
            else unknown = true;
        } else if (fact.source == "udev:ID_CDROM" || fact.source == "udev:ID_DRIVE_FLOPPY") {
            const auto flag = parseBoolean(value, "physical_kind", name, issues);
            if (!flag) unknown = true;
            else if (*flag) {
                if (fact.source == "udev:ID_CDROM") optical = true;
                else floppy = true;
            }
        } else if (fact.source == "sysfs:parent/subsystem" && value == "nvme") {
            nvme = disk = true;
        } else unknown = true;
    }
    // A USB/UFI floppy can report SCSI direct-access type 0. More specific
    // floppy evidence narrows that class; optical/direct-disk or NVMe/floppy
    // disagreement is a conflict. Names, size, rotation and transport never
    // manufacture a disk classification.
    if ((optical && (disk || floppy)) || (nvme && floppy)) {
        addIssue(issues, BlockInventoryIssueCode::ConflictingAttribute, name,
                 "physical_kind", "Structured device-kind evidence disagrees");
        return Kind::UnknownPhysical;
    }
    if (unknown || unreliable(issues, "physical_kind")) return Kind::UnknownPhysical;
    if (floppy) return Kind::Floppy;
    if (optical) return Kind::Optical;
    if (disk) return Kind::Disk;
    return Kind::UnknownPhysical;
}

BlockDeviceObservation mapRecord(const LinuxBlockSourceRecord& record,
                                 const std::string& kernel_name) {
    BlockDeviceObservation observation;
    observation.kernel_name = kernel_name;
    observation.observation_kind = record.observation_kind;
    observation.issues = record.issues;
    observation.model_observations = record.model_observations;
    observation.device_kind_observations = record.device_kind_observations;
    observation.physical_kind = physicalKind(record, kernel_name, observation.issues);
    for (BlockInventoryIssue& issue : observation.issues) {
        if (!issue.kernel_name) issue.kernel_name = kernel_name;
    }

    observation.current_path = parsePath(
        record.current_path, "/dev/", "current_path", kernel_name, observation.issues);
    observation.sysfs_path = parsePath(
        record.sysfs_path, "/sys/", "sysfs_path", kernel_name, observation.issues);
    observation.node_type = parseNodeType(record.node_type, kernel_name, observation.issues);
    observation.device_number = parseDeviceNumber(record, kernel_name, observation.issues);
    observation.capacity_bytes = parseCapacity(
        record.capacity_sectors_512, kernel_name, observation.issues);
    observation.vendor = parseText(record.vendor, "vendor", kernel_name, observation.issues);
    observation.model = parseText(record.model, "model", kernel_name, observation.issues);
    observation.serial = parseText(record.serial, "serial", kernel_name, observation.issues);
    observation.wwn = parseText(record.wwn, "wwn", kernel_name, observation.issues);
    observation.nvme_eui = parseNvmeIdentifier(
        record.nvme_eui, 16, "nvme_eui", kernel_name, observation.issues);
    observation.nvme_nguid = parseNvmeIdentifier(
        record.nvme_nguid, 32, "nvme_nguid", kernel_name, observation.issues);
    observation.rotational = parseBoolean(
        record.rotational, "rotational", kernel_name, observation.issues);
    observation.removable = parseBoolean(
        record.removable, "removable", kernel_name, observation.issues);
    observation.read_only = parseBoolean(
        record.read_only, "read_only", kernel_name, observation.issues);
    observation.transport = parseText(
        record.transport, "transport", kernel_name, observation.issues);
    observation.parent_kernel_name = parseText(
        record.parent_kernel_name, "parent_kernel_name", kernel_name, observation.issues);
    observation.partition_number = parseUnsigned<std::uint32_t>(
        record.partition_number, "partition_number", kernel_name, observation.issues);
    if (observation.partition_number && (*observation.partition_number == 0 ||
        observation.node_type != BlockNodeType::Partition)) {
        addIssue(observation.issues, BlockInventoryIssueCode::InvalidRelationship,
                 kernel_name, "partition_number",
                 "A partition number must be positive and belong to a partition");
        observation.partition_number.reset();
    }
    normalizeIssues(observation.issues);
    return observation;
}

template <typename Value, typename Getter>
std::optional<Value> mergeOptional(const std::vector<BlockDeviceObservation>& observations,
                                   Getter getter,
                                   const std::string& field,
                                   const std::string& kernel_name,
                                   std::vector<BlockInventoryIssue>& issues) {
    std::vector<Value> distinct;
    bool unknown = false;
    for (const BlockDeviceObservation& observation : observations) {
        const std::optional<Value>& value = getter(observation);
        unknown = unknown || !value;
        if (value && std::find(distinct.begin(), distinct.end(), *value) == distinct.end()) {
            distinct.push_back(*value);
        }
    }
    if (distinct.size() == 1 && !unknown) return distinct.front();
    if (!distinct.empty()) {
        addIssue(issues, BlockInventoryIssueCode::DuplicateRecord, kernel_name,
                 field, "Duplicate source records disagree or have incomplete evidence");
    }
    return std::nullopt;
}

template <typename Value, typename Getter>
Value mergeEnum(const std::vector<BlockDeviceObservation>& observations,
                Getter getter,
                Value unknown,
                const std::string& field,
                const std::string& kernel_name,
                std::vector<BlockInventoryIssue>& issues) {
    std::vector<Value> distinct;
    bool has_unknown = false;
    for (const BlockDeviceObservation& observation : observations) {
        const Value value = getter(observation);
        has_unknown = has_unknown || value == unknown;
        if (value != unknown && std::find(distinct.begin(), distinct.end(), value) == distinct.end()) {
            distinct.push_back(value);
        }
    }
    if (distinct.size() == 1 && !has_unknown) return distinct.front();
    if (!distinct.empty()) {
        addIssue(issues, BlockInventoryIssueCode::DuplicateRecord, kernel_name,
                 field, "Duplicate source records disagree or have incomplete evidence");
    }
    return unknown;
}

BlockDeviceObservation mergeRecords(const std::string& kernel_name,
                                    const std::vector<BlockDeviceObservation>& observations) {
    BlockDeviceObservation merged;
    merged.kernel_name = kernel_name;
    for (const BlockDeviceObservation& observation : observations) {
        merged.issues.insert(merged.issues.end(), observation.issues.begin(),
                             observation.issues.end());
        merged.device_kind_observations.insert(merged.device_kind_observations.end(),
            observation.device_kind_observations.begin(), observation.device_kind_observations.end());
        merged.model_observations.insert(merged.model_observations.end(),
            observation.model_observations.begin(), observation.model_observations.end());
    }
    std::sort(merged.model_observations.begin(), merged.model_observations.end(),
        [](const auto& a, const auto& b) { return std::tie(a.source, a.value) < std::tie(b.source, b.value); });
    merged.model_observations.erase(std::unique(merged.model_observations.begin(),
        merged.model_observations.end()), merged.model_observations.end());
    if (observations.size() > 1) {
        addIssue(merged.issues, BlockInventoryIssueCode::DuplicateRecord, kernel_name,
                 "kernel_name", "Multiple source records reported the same kernel name");
    }

    merged.current_path = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.current_path; },
        "current_path", kernel_name, merged.issues);
    merged.sysfs_path = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.sysfs_path; },
        "sysfs_path", kernel_name, merged.issues);
    std::sort(merged.device_kind_observations.begin(), merged.device_kind_observations.end(),
        [](const auto& a, const auto& b) { return std::tie(a.source,a.value) < std::tie(b.source,b.value); });
    merged.device_kind_observations.erase(std::unique(merged.device_kind_observations.begin(),
        merged.device_kind_observations.end()), merged.device_kind_observations.end());
    merged.physical_kind = mergeEnum<PhysicalDeviceKind>(
        observations, [](const auto& value) { return value.physical_kind; }, PhysicalDeviceKind::UnknownPhysical,
        "physical_kind", kernel_name, merged.issues);
    merged.node_type = mergeEnum<BlockNodeType>(
        observations, [](const auto& value) { return value.node_type; }, BlockNodeType::Unknown,
        "node_type", kernel_name, merged.issues);
    merged.device_number = mergeOptional<BlockDeviceNumber>(
        observations, [](const auto& value) -> const auto& { return value.device_number; },
        "device_number", kernel_name, merged.issues);
    merged.capacity_bytes = mergeOptional<std::uint64_t>(
        observations, [](const auto& value) -> const auto& { return value.capacity_bytes; },
        "capacity_bytes", kernel_name, merged.issues);
    merged.vendor = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.vendor; },
        "vendor", kernel_name, merged.issues);
    merged.model = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.model; },
        "model", kernel_name, merged.issues);
    merged.serial = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.serial; },
        "serial", kernel_name, merged.issues);
    merged.wwn = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.wwn; },
        "wwn", kernel_name, merged.issues);
    merged.nvme_eui = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.nvme_eui; },
        "nvme_eui", kernel_name, merged.issues);
    merged.nvme_nguid = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.nvme_nguid; },
        "nvme_nguid", kernel_name, merged.issues);
    merged.rotational = mergeOptional<bool>(
        observations, [](const auto& value) -> const auto& { return value.rotational; },
        "rotational", kernel_name, merged.issues);
    merged.removable = mergeOptional<bool>(
        observations, [](const auto& value) -> const auto& { return value.removable; },
        "removable", kernel_name, merged.issues);
    merged.read_only = mergeOptional<bool>(
        observations, [](const auto& value) -> const auto& { return value.read_only; },
        "read_only", kernel_name, merged.issues);
    merged.transport = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.transport; },
        "transport", kernel_name, merged.issues);
    merged.parent_kernel_name = mergeOptional<std::string>(
        observations, [](const auto& value) -> const auto& { return value.parent_kernel_name; },
        "parent_kernel_name", kernel_name, merged.issues);
    merged.partition_number = mergeOptional<std::uint32_t>(
        observations, [](const auto& value) -> const auto& { return value.partition_number; },
        "partition_number", kernel_name, merged.issues);
    merged.observation_kind = mergeEnum<BlockObservationKind>(
        observations, [](const auto& value) { return value.observation_kind; },
        BlockObservationKind::Unknown, "observation_kind", kernel_name, merged.issues);

    normalizeIssues(merged.issues);
    return merged;
}

// Detect locator collisions across different kernel names without choosing a winner.
// These are scan-local locators, never stable physical identities.
template <typename Getter>
void rejectLocatorCollisions(BlockInventorySnapshot& snapshot, Getter getter,
                             const std::string& field) {
    std::vector<bool> conflicting(snapshot.devices.size(), false);
    for (std::size_t i = 0; i < snapshot.devices.size(); ++i) {
        const auto& left = getter(snapshot.devices[i]);
        if (!left) continue;
        for (std::size_t j = i + 1; j < snapshot.devices.size(); ++j) {
            if (left == getter(snapshot.devices[j])) conflicting[i] = conflicting[j] = true;
        }
    }
    for (std::size_t i = 0; i < snapshot.devices.size(); ++i) {
        if (!conflicting[i]) continue;
        auto& observation = snapshot.devices[i];
        getter(observation).reset();
        addIssue(observation.issues, BlockInventoryIssueCode::DuplicateRecord,
                 observation.kernel_name, field,
                 "Different kernel names reported the same scan-local locator");
    }
}

void constructRelationships(BlockInventorySnapshot& snapshot) {
    std::map<std::string, std::size_t> indices;
    for (std::size_t index = 0; index < snapshot.devices.size(); ++index) {
        indices.emplace(snapshot.devices[index].kernel_name, index);
    }

    for (BlockDeviceObservation& child : snapshot.devices) {
        if (!child.parent_kernel_name) continue;
        if (child.node_type != BlockNodeType::Partition) {
            addIssue(child.issues, BlockInventoryIssueCode::InvalidRelationship,
                     child.kernel_name, "parent_kernel_name",
                     "Only partition-to-disk parent relationships are accepted in 0.3-A");
            child.parent_kernel_name.reset();
            continue;
        }

        const auto parent = indices.find(*child.parent_kernel_name);
        if (parent == indices.end()) {
            addIssue(child.issues, BlockInventoryIssueCode::InvalidRelationship,
                     child.kernel_name, "parent_kernel_name",
                     "The observed parent disk was not present in this snapshot");
            continue;
        }
        BlockDeviceObservation& parent_observation = snapshot.devices[parent->second];
        if (parent_observation.node_type != BlockNodeType::Disk) {
            addIssue(child.issues, BlockInventoryIssueCode::InvalidRelationship,
                     child.kernel_name, "parent_kernel_name",
                     "The observed parent is not a disk node");
            child.parent_kernel_name.reset();
            continue;
        }
        parent_observation.child_kernel_names.push_back(child.kernel_name);
    }

    for (BlockDeviceObservation& observation : snapshot.devices) {
        std::sort(observation.child_kernel_names.begin(),
                  observation.child_kernel_names.end());
        observation.child_kernel_names.erase(
            std::unique(observation.child_kernel_names.begin(),
                        observation.child_kernel_names.end()),
            observation.child_kernel_names.end());
        normalizeIssues(observation.issues);
    }
}

}  // namespace

LinuxInventoryProvider::LinuxInventoryProvider(LinuxInventorySource& source)
    : source_(source) {}

Result<BlockInventorySnapshot> LinuxInventoryProvider::scan() {
    ScanStage timing("inventory.raw");
    Result<LinuxInventorySourceSnapshot> source_snapshot = source_.scan();
    if (!source_snapshot) {
        return Result<BlockInventorySnapshot>::failure(source_snapshot.error());
    }

    BlockInventorySnapshot snapshot;
    snapshot.issues = source_snapshot.value().issues;
    std::map<std::string, std::vector<BlockDeviceObservation>> observations_by_name;
    for (const LinuxBlockSourceRecord& record : source_snapshot.value().records) {
        const std::string kernel_name = trim(record.kernel_name);
        if (kernel_name.empty()) {
            snapshot.issues.insert(snapshot.issues.end(), record.issues.begin(),
                                   record.issues.end());
            addIssue(snapshot.issues, BlockInventoryIssueCode::MissingRequiredField,
                     std::nullopt, "kernel_name",
                     "A source record without a kernel name was discarded");
            continue;
        }
        if (kernel_name == "." || kernel_name == ".." ||
            kernel_name.find('/') != std::string::npos ||
            std::any_of(kernel_name.begin(), kernel_name.end(), [](unsigned char c) {
                return c <= 32 || c == 127;
            })) {
            snapshot.issues.insert(snapshot.issues.end(), record.issues.begin(), record.issues.end());
            addIssue(snapshot.issues, BlockInventoryIssueCode::MalformedAttribute,
                     kernel_name, "kernel_name", "An invalid kernel name was discarded");
            continue;
        }
        observations_by_name[kernel_name].push_back(mapRecord(record, kernel_name));
    }

    snapshot.devices.reserve(observations_by_name.size());
    for (const auto& [kernel_name, observations] : observations_by_name) {
        snapshot.devices.push_back(mergeRecords(kernel_name, observations));
    }
    rejectLocatorCollisions(snapshot,
        [](auto& value) -> auto& { return value.current_path; }, "current_path");
    rejectLocatorCollisions(snapshot,
        [](auto& value) -> auto& { return value.sysfs_path; }, "sysfs_path");
    rejectLocatorCollisions(snapshot,
        [](auto& value) -> auto& { return value.device_number; }, "device_number");
    constructRelationships(snapshot);
    std::sort(snapshot.devices.begin(), snapshot.devices.end(),
              [](const BlockDeviceObservation& left, const BlockDeviceObservation& right) {
                  return left.kernel_name < right.kernel_name;
              });
    normalizeIssues(snapshot.issues);
    return Result<BlockInventorySnapshot>::success(std::move(snapshot));
}

}  // namespace drivelab
