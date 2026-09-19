#include "platform/linux/linux_host_usage.h"

#include <libudev.h>

#include <cerrno>
#include <charconv>
#include <memory>

namespace drivelab {
namespace {

struct ContextDeleter { void operator()(udev* value) const { if (value) udev_unref(value); } };
struct DeviceDeleter { void operator()(udev_device* value) const { if (value) udev_device_unref(value); } };
using Context = std::unique_ptr<udev, ContextDeleter>;
using Device = std::unique_ptr<udev_device, DeviceDeleter>;

void problem(HostRead<SignatureSourceRecord>& result, const BlockDeviceObservation& node,
             const std::string& field, const std::string& message, int error) {
    result.issues.push_back({
        error == ENOENT || error == ENODEV || error == ENXIO
            ? DiscoveryIssueCode::Disappeared : DiscoveryIssueCode::ReadFailure,
        node.kernel_name, field, message,
        error == 0 ? std::nullopt : std::optional<int>(error)});
}

std::optional<std::uint32_t> number(udev_device* device, const char* property) {
    const char* raw = udev_device_get_property_value(device, property);
    if (!raw) return std::nullopt;
    const std::string text(raw);
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

bool sameNode(udev_device* device, const BlockDeviceObservation& node) {
    const char* name = udev_device_get_sysname(device);
    if (!name || node.kernel_name != name || !node.device_number) return false;
    const auto major = number(device, "MAJOR");
    const auto minor = number(device, "MINOR");
    return major && minor && *major == node.device_number->major_number &&
           *minor == node.device_number->minor_number;
}

}  // namespace

HostRead<SignatureSourceRecord> UdevSignatureSource::read(const BlockDeviceObservation& node) {
    HostRead<SignatureSourceRecord> result;
    if (!node.sysfs_path || !node.sysfs_path->starts_with("/sys/") ||
        node.sysfs_path->find("/../") != std::string::npos ||
        node.sysfs_path->ends_with("/..") || node.sysfs_path->find('\0') != std::string::npos) {
        result.issues.push_back({DiscoveryIssueCode::MissingEvidence, node.kernel_name,
                                "signature", "No usable raw sysfs locator for udev metadata"});
        return result;
    }
    errno = 0;
    Context context(udev_new());
    if (!context) { problem(result, node, "signature", "Cannot initialize libudev", errno); return result; }
    errno = 0;
    Device device(udev_device_new_from_syspath(context.get(), node.sysfs_path->c_str()));
    if (!device) { problem(result, node, "signature", "Cannot read observed udev node", errno); return result; }
    if (!sameNode(device.get(), node)) {
        result.issues.push_back({DiscoveryIssueCode::SnapshotChanged, node.kernel_name,
                                "signature", "udev metadata no longer matches raw kernel name/device number"});
        return result;
    }
    SignatureSourceRecord record;
    for (const char* key : {"ID_FS_TYPE", "ID_FS_USAGE", "ID_FS_UUID", "ID_FS_LABEL",
                            "ID_FS_AMBIVALENT"}) {
        errno = 0;
        const char* value = udev_device_get_property_value(device.get(), key);
        const int error = errno;
        if (value) record.properties.emplace_back(key, value);
        else if (error != 0 && error != ENOENT) {
            problem(result, node, key, "Cannot read cached udev signature property", error);
        }
    }
    // A fresh object checks existence/device number, but not every reuse race.
    errno = 0;
    Device after(udev_device_new_from_syspath(context.get(), node.sysfs_path->c_str()));
    if (!after) { problem(result, node, "signature", "Node disappeared during signature read", errno); return result; }
    if (!sameNode(after.get(), node)) {
        result.issues.push_back({DiscoveryIssueCode::SnapshotChanged, node.kernel_name,
                                "signature", "Node changed during signature acquisition"});
        return result;
    }
    result.value = std::move(record);
    return result;
}

}  // namespace drivelab
