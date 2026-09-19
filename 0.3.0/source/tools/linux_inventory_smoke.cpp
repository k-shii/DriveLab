#include "platform/linux/linux_inventory_provider.h"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace drivelab {
namespace {

// Escape control bytes so raw metadata/issues cannot inject terminal commands
// or masquerade as additional output fields.
void writeText(std::ostream& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\' << static_cast<char>(character);
        } else if (character < 32 || character >= 127) {
            output << "\\x" << hex[character >> 4] << hex[character & 15];
        } else {
            output << static_cast<char>(character);
        }
    }
    output << '"';
}

template <typename T>
void writeValue(std::ostream& output, const T& value) {
    output << value;
}

void writeValue(std::ostream& output, const std::string& value) {
    writeText(output, value);
}

void writeValue(std::ostream& output, bool value) {
    output << (value ? "true" : "false");
}

void writeValue(std::ostream& output, const BlockDeviceNumber& value) {
    output << value.major_number << ':' << value.minor_number;
}

template <typename T>
void writeField(std::ostream& output, const char* name, const std::optional<T>& value) {
    output << "  " << name << ": ";
    if (value) writeValue(output, *value);
    else output << "unknown";
    output << '\n';
}

const char* nodeTypeName(BlockNodeType type) {
    switch (type) {
        case BlockNodeType::Disk: return "disk";
        case BlockNodeType::Partition: return "partition";
        case BlockNodeType::Unknown: return "unknown";
    }
    return "unknown";
}

const char* issueName(BlockInventoryIssueCode code) {
    switch (code) {
        case BlockInventoryIssueCode::MissingRequiredField: return "MissingRequiredField";
        case BlockInventoryIssueCode::MalformedAttribute: return "MalformedAttribute";
        case BlockInventoryIssueCode::CapacityOverflow: return "CapacityOverflow";
        case BlockInventoryIssueCode::DuplicateRecord: return "DuplicateRecord";
        case BlockInventoryIssueCode::DeviceDisappeared: return "DeviceDisappeared";
        case BlockInventoryIssueCode::SourceReadFailure: return "SourceReadFailure";
        case BlockInventoryIssueCode::ConflictingAttribute: return "ConflictingAttribute";
        case BlockInventoryIssueCode::InvalidRelationship: return "InvalidRelationship";
    }
    return "unrecognized_issue";
}

void writeIssues(std::ostream& output, const std::vector<BlockInventoryIssue>& issues) {
    output << "  issues: " << issues.size() << '\n';
    for (const auto& issue : issues) {
        output << "    " << issueName(issue.code) << " kernel_name=";
        if (issue.kernel_name) writeText(output, *issue.kernel_name);
        else output << "unknown";
        output << " field=";
        writeText(output, issue.field);
        output << " native_error=";
        if (issue.native_error) output << *issue.native_error;
        else output << "unavailable";
        output << " message=";
        writeText(output, issue.message);
        output << '\n';
    }
}

void writeSnapshot(std::ostream& output, const BlockInventorySnapshot& snapshot) {
    output << "DriveLab 0.3-A raw inventory - manual development smoke check\n"
           << "Observations only; no identity or safety decisions. Scan is not atomic.\n"
           << "devices: " << snapshot.devices.size() << '\n';
    for (const auto& device : snapshot.devices) {
        output << "\nblock_device:\n  kernel_name: ";
        writeText(output, device.kernel_name);
        output << "\n  node_type: " << nodeTypeName(device.node_type) << '\n';
        writeField(output, "current_path", device.current_path);
        writeField(output, "sysfs_path", device.sysfs_path);
        writeField(output, "device_number", device.device_number);
        writeField(output, "capacity_bytes", device.capacity_bytes);
        writeField(output, "vendor", device.vendor);
        writeField(output, "model", device.model);
        for (const auto& observed : device.model_observations) {
            output << "  model_source="; writeText(output, observed.source);
            output << " value="; writeText(output, observed.value); output << '\n';
        }
        writeField(output, "serial", device.serial);
        writeField(output, "wwn", device.wwn);
        writeField(output, "nvme_eui", device.nvme_eui);
        writeField(output, "nvme_nguid", device.nvme_nguid);
        writeField(output, "rotational", device.rotational);
        writeField(output, "removable", device.removable);
        writeField(output, "read_only", device.read_only);
        writeField(output, "transport", device.transport);
        writeField(output, "parent_kernel_name", device.parent_kernel_name);
        writeField(output, "partition_number", device.partition_number);
        output << "  origin_evidence: "
               << (device.observation_kind == BlockObservationKind::VirtualOrPseudo
                       ? "virtual_or_pseudo" : "unknown")
               << "\n  child_kernel_names: [";
        bool first = true;
        for (const auto& child : device.child_kernel_names) {
            if (!first) output << ", ";
            writeText(output, child);
            first = false;
        }
        output << "]\n";
        writeIssues(output, device.issues);
    }
    output << "\nsnapshot:\n";
    writeIssues(output, snapshot.issues);
}

}  // namespace

int runInventorySmoke(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: drivelab_inventory_smoke [--help]\n"
                  << "Manual development-only, one-shot libudev/sysfs inventory.\n"
                  << "With no arguments, reads host metadata and prints raw observations/issues.\n"
                  << "No block-device I/O, storage commands, identity or safety decisions.\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "Usage: drivelab_inventory_smoke [--help]\n";
        return 2;
    }

    UdevSysfsInventorySource source;
    LinuxInventoryProvider provider(source);
    const auto result = provider.scan();
    if (!result) {
        const auto& error = result.error();
        std::cerr << "inventory_source_error code=" << static_cast<int>(error.code)
                  << " component=";
        writeText(std::cerr, error.component);
        std::cerr << " message=";
        writeText(std::cerr, error.message);
        std::cerr << '\n';
        return 1;
    }

    writeSnapshot(std::cout, result.value());
    return std::cout ? 0 : 1;
}

}  // namespace drivelab

int main(int argc, char* argv[]) {
    return drivelab::runInventorySmoke(argc, argv);
}
