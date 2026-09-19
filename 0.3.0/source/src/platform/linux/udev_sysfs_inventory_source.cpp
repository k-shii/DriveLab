#include "platform/linux/linux_inventory_source.h"

#include <libudev.h>

#include <algorithm>
#include <cerrno>
#include <system_error>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace drivelab {
namespace {

template <typename Type, Type* (*Unref)(Type*)>
struct UdevDeleter {
    void operator()(Type* value) const {
        if (value != nullptr) Unref(value);
    }
};

using UdevContext = std::unique_ptr<udev, UdevDeleter<udev, udev_unref>>;
using UdevEnumeration =
    std::unique_ptr<udev_enumerate, UdevDeleter<udev_enumerate, udev_enumerate_unref>>;
using UdevDevice =
    std::unique_ptr<udev_device, UdevDeleter<udev_device, udev_device_unref>>;

bool disappeared(int error) {
    return error == ENOENT || error == ENODEV || error == ENXIO;
}

std::string errorMessage(std::string message, int error) {
    if (error != 0) {
        message += ": ";
        message += std::system_category().message(error);
    }
    return message;
}

BlockInventoryIssue readIssue(std::optional<std::string> name, std::string field,
                              std::string message, int error, bool device_read) {
    return {
        ((device_read && disappeared(error)) || error == ENODEV || error == ENXIO)
            ? BlockInventoryIssueCode::DeviceDisappeared
            : BlockInventoryIssueCode::SourceReadFailure,
        std::move(name), std::move(field), errorMessage(std::move(message), error),
        error == 0 ? std::nullopt : std::optional<int>(error)
    };
}

// libudev caches attribute values. Copy borrowed strings immediately and capture
// errno before any other call. ENOENT on a field means absent, not a zero value.
template <typename Read>
std::optional<std::string> readValue(LinuxBlockSourceRecord& record,
                                     const char* field,
                                     const std::string& location, Read read) {
    errno = 0;
    const char* value = read();
    const int error = errno;
    if (value != nullptr) return std::string(value);
    if (error != 0 && error != ENOENT) {
        record.issues.push_back(readIssue(record.kernel_name, field,
            "Unable to read " + location, error, false));
    }
    return std::nullopt;
}

std::optional<std::string> propertyValue(udev_device* device,
                                         LinuxBlockSourceRecord& record,
                                         const char* field, const char* name) {
    return readValue(record, field, std::string("udev property ") + name, [&] {
        return udev_device_get_property_value(device, name);
    });
}

std::optional<std::string> sysfsValue(udev_device* device,
                                      LinuxBlockSourceRecord& record,
                                      const char* field, const char* name) {
    return readValue(record, field, std::string("sysfs attribute ") + name, [&] {
        return udev_device_get_sysattr_value(device, name);
    });
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n\f\v") - first + 1);
}

// Fields remain conflicting unless the model-specific source contract below
// proves a representation difference. Never override a failed source read.
std::optional<std::string> agreeingText(
    LinuxBlockSourceRecord& record, const char* field,
    std::optional<std::string> sysfs, std::optional<std::string> property) {
    if (sysfs && property && trim(*sysfs) != trim(*property)) {
        record.issues.push_back({
            BlockInventoryIssueCode::ConflictingAttribute, record.kernel_name, field,
            "sysfs value [" + *sysfs + "] disagrees with udev value [" + *property + "]"
        });
        return std::nullopt;
    }
    // A failed alternative must not be silently replaced by another source.
    if (std::any_of(record.issues.begin(), record.issues.end(), [&](const auto& issue) {
            return issue.field == field;
        })) return std::nullopt;
    return sysfs ? sysfs : property;
}

// ID_MODEL_ENC preserves ATA model bytes, unlike the lossy ID_MODEL spelling.
std::optional<std::string> ataModel(const std::string& encoded) {
    std::string raw;
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(encoded[i]);
        if (c == '\\') {
            if (i + 3 >= encoded.size() || encoded[i + 1] != 'x') return std::nullopt;
            const auto high = hex(encoded[i + 2]), low = hex(encoded[i + 3]);
            if (high < 0 || low < 0) return std::nullopt;
            c = static_cast<unsigned char>(high * 16 + low); i += 3;
        }
        if (c < 32 || c >= 127 || raw.size() >= 40) return std::nullopt;
        raw += static_cast<char>(c);
    }
    return raw.empty() ? std::nullopt : std::optional<std::string>(raw);
}
std::string udevModelSpelling(const std::string& raw) {
    std::string result;
    bool space = false;
    for (char c : trim(raw)) {
        if (c == ' ') { space = true; continue; }
        if (space) result += '_';
        space = false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return "";
        result += c;
    }
    return result;
}
std::optional<std::string> readModel(udev_device* device, LinuxBlockSourceRecord& record) {
    auto sysfs = sysfsValue(device, record, "model", "device/model");
    auto property = propertyValue(device, record, "model", "ID_MODEL");
    auto encoded = propertyValue(device, record, "model", "ID_MODEL_ENC");
    for (const auto& [source, value] : std::vector<std::pair<std::string, std::optional<std::string>>>{
            {"sysfs:device/model", sysfs}, {"udev:ID_MODEL", property}, {"udev:ID_MODEL_ENC", encoded}})
        if (value) record.model_observations.push_back({source, *value});
    if (sysfs && property && trim(*sysfs) != trim(*property) && encoded &&
        record.transport == "ata") {
        const auto ata = propertyValue(device, record, "model", "ID_ATA");
        if (ata) record.model_observations.push_back({"udev:ID_ATA", *ata});
        const auto raw = ataModel(*encoded);
        const bool clean = std::none_of(record.issues.begin(), record.issues.end(),
            [](const auto& i) { return i.field == "model" || i.field == "transport"; });
        // The SCSI product is exactly the first 16 ATA model bytes.
        // Corroborate BOTH spellings; never guess whether an underscore was a space.
        if (clean && ata == "1" && raw && udevModelSpelling(*raw) == trim(*property) &&
            (trim(*sysfs) == trim(*raw) ||
             (trim(*sysfs).size() == 16 && trim(*sysfs) == raw->substr(0, 16))))
            return trim(*raw);
    }
    return agreeingText(record, "model", std::move(sysfs), std::move(property));
}

std::optional<std::string> kernelNameFromSyspath(const char* syspath) {
    if (syspath == nullptr) return std::nullopt;
    const std::string path(syspath);
    const auto separator = path.find_last_of('/');
    const std::string name = separator == std::string::npos
        ? path : path.substr(separator + 1);
    return name.empty() ? std::nullopt : std::optional<std::string>(name);
}

Error enumerationError(ErrorCode code, std::string message, int error = 0) {
    if (error == EACCES || error == EPERM) code = ErrorCode::PermissionDenied;
    return {code, "UdevSysfsInventorySource", errorMessage(std::move(message), error)};
}

LinuxBlockSourceRecord readRecord(udev_device* device) {
    LinuxBlockSourceRecord record;
    record.kernel_name = readValue(record, "kernel_name", "udev sysname", [&] {
        return udev_device_get_sysname(device);
    }).value_or("");
    record.sysfs_path = readValue(record, "sysfs_path", "udev syspath", [&] {
        return udev_device_get_syspath(device);
    });
    record.current_path = readValue(record, "current_path", "udev devnode", [&] {
        return udev_device_get_devnode(device);
    });
    record.node_type = readValue(record, "node_type", "udev devtype", [&] {
        return udev_device_get_devtype(device);
    });
    // These properties come from the block uevent. Do not manufacture missing
    // numbers from an ambiguous zero dev_t or overwrite a partially read pair.
    record.major_number = propertyValue(device, record, "major_number", "MAJOR");
    record.minor_number = propertyValue(device, record, "minor_number", "MINOR");
    record.capacity_sectors_512 = sysfsValue(device, record, "capacity_sectors_512", "size");

    // Sequence reads explicitly: both calls append to the same issue collection.
    auto vendor = sysfsValue(device, record, "vendor", "device/vendor");
    auto vendor_property = propertyValue(device, record, "vendor", "ID_VENDOR");
    record.vendor = agreeingText(record, "vendor", std::move(vendor), std::move(vendor_property));
    record.transport = propertyValue(device, record, "transport", "ID_BUS");
    record.model = readModel(device, record);
    auto serial = sysfsValue(device, record, "serial", "device/serial");
    auto serial_property = propertyValue(device, record, "serial", "ID_SERIAL_SHORT");
    record.serial = agreeingText(record, "serial", std::move(serial), std::move(serial_property));

    // WWID can encode a UUID, NGUID, EUI, or a synthetic identifier. It is not
    // a WWN fallback. Namespace EUI/NGUID live on the block node itself.
    record.wwn = propertyValue(device, record, "wwn", "ID_WWN");
    record.nvme_eui = sysfsValue(device, record, "nvme_eui", "eui");
    record.nvme_nguid = sysfsValue(device, record, "nvme_nguid", "nguid");
    record.rotational = sysfsValue(device, record, "rotational", "queue/rotational");
    record.removable = sysfsValue(device, record, "removable", "removable");
    record.read_only = sysfsValue(device, record, "read_only", "ro");
    record.partition_number = sysfsValue(device, record, "partition_number", "partition");

    if (record.node_type == "disk" && record.sysfs_path &&
        !record.sysfs_path->starts_with("/sys/devices/virtual/")) {
        for (const auto* property : {"ID_TYPE", "ID_USB_TYPE", "ID_CDROM", "ID_DRIVE_FLOPPY"}) {
            if (auto value = propertyValue(device, record, "physical_kind", property))
                record.device_kind_observations.push_back({std::string("udev:") + property, *value});
        }
        // Kernel subsystem ancestry is evidence; the device's name is not.
        errno = 0;
        const auto* scsi = udev_device_get_parent_with_subsystem_devtype(device, "scsi", nullptr);
        const int scsi_error = errno;
        if (scsi) {
            if (auto value = sysfsValue(device, record, "physical_kind", "device/type"))
                record.device_kind_observations.push_back({"sysfs:scsi/type", *value});
        } else if (scsi_error && scsi_error != ENOENT) {
            record.issues.push_back(readIssue(record.kernel_name, "physical_kind",
                "Cannot inspect SCSI ancestry", scsi_error, false));
        }
        errno = 0;
        const auto* nvme = udev_device_get_parent_with_subsystem_devtype(device, "nvme", nullptr);
        const int nvme_error = errno;
        if (nvme) {
            record.device_kind_observations.push_back({"sysfs:parent/subsystem", "nvme"});
        } else if (nvme_error && nvme_error != ENOENT) {
            record.issues.push_back(readIssue(record.kernel_name, "physical_kind",
                "Cannot inspect NVMe ancestry", nvme_error, false));
        }
    }

    if (record.sysfs_path &&
        std::string_view(*record.sysfs_path).starts_with("/sys/devices/virtual/")) {
        record.observation_kind = BlockObservationKind::VirtualOrPseudo;
    }
    // A location outside /virtual does not prove a physical drive (e.g. virtio).
    if (record.node_type && *record.node_type == "partition") {
        errno = 0;
        udev_device* parent = udev_device_get_parent_with_subsystem_devtype(device, "block", "disk");
        const int error = errno;
        if (parent != nullptr) {
            record.parent_kernel_name = readValue(record, "parent_kernel_name", "udev parent sysname", [&] {
                return udev_device_get_sysname(parent);
            });
        } else {
            record.issues.push_back(readIssue(record.kernel_name, "parent_kernel_name",
                "Unable to observe the partition's parent disk", error, false));
        }
    }
    return record;
}

}  // namespace

Result<LinuxInventorySourceSnapshot> UdevSysfsInventorySource::scan() {
    errno = 0;
    UdevContext context(udev_new());
    if (!context) {
        return Result<LinuxInventorySourceSnapshot>::failure(enumerationError(
            ErrorCode::Unavailable, "Unable to initialize libudev", errno));
    }

    errno = 0;
    UdevEnumeration enumeration(udev_enumerate_new(context.get()));
    if (!enumeration) {
        return Result<LinuxInventorySourceSnapshot>::failure(enumerationError(
            ErrorCode::Unavailable, "Unable to allocate a libudev enumeration", errno));
    }

    const int match_result = udev_enumerate_add_match_subsystem(enumeration.get(), "block");
    if (match_result < 0) {
        return Result<LinuxInventorySourceSnapshot>::failure(enumerationError(
            ErrorCode::IoError, "Unable to select the block subsystem", -match_result));
    }
    const int scan_result = udev_enumerate_scan_devices(enumeration.get());
    if (scan_result < 0) {
        return Result<LinuxInventorySourceSnapshot>::failure(enumerationError(
            ErrorCode::IoError, "Unable to enumerate block devices", -scan_result));
    }

    LinuxInventorySourceSnapshot snapshot;
    errno = 0;
    udev_list_entry* first = udev_enumerate_get_list_entry(enumeration.get());
    if (first == nullptr && errno != 0) {
        return Result<LinuxInventorySourceSnapshot>::failure(enumerationError(
            ErrorCode::IoError, "Unable to read the libudev enumeration", errno));
    }
    for (udev_list_entry* entry = first; entry != nullptr;
         entry = udev_list_entry_get_next(entry)) {
        const char* syspath = udev_list_entry_get_name(entry);
        if (syspath == nullptr) {
            snapshot.issues.push_back({
                BlockInventoryIssueCode::SourceReadFailure, std::nullopt, "sysfs_path",
                "libudev returned an enumeration entry without a sysfs path"
            });
            continue;
        }

        errno = 0;
        UdevDevice device(udev_device_new_from_syspath(context.get(), syspath));
        if (!device) {
            snapshot.issues.push_back(readIssue(kernelNameFromSyspath(syspath), "sysfs_path",
                "Unable to read the enumerated block node", errno, true));
            continue;
        }

        auto record = readRecord(device.get());
        // A fresh metadata object avoids libudev's attribute cache for this
        // existence check. This is not an atomic scan or identity re-resolution.
        errno = 0;
        UdevDevice still_present(udev_device_new_from_syspath(context.get(), syspath));
        const int end_error = errno;
        const bool vanished = std::any_of(record.issues.begin(), record.issues.end(),
            [](const auto& issue) { return issue.code == BlockInventoryIssueCode::DeviceDisappeared; });
        if (!still_present || vanished) {
            snapshot.issues.insert(snapshot.issues.end(), record.issues.begin(), record.issues.end());
            if (!still_present) {
                snapshot.issues.push_back(readIssue(kernelNameFromSyspath(syspath), "sysfs_path",
                    "Unable to confirm the block node after reading metadata", end_error, true));
            }
            continue;
        }
        snapshot.records.push_back(std::move(record));
    }

    return Result<LinuxInventorySourceSnapshot>::success(std::move(snapshot));
}

}  // namespace drivelab

