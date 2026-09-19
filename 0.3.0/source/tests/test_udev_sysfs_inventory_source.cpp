#include "test_support.h"

#include "platform/linux/linux_inventory_provider.h"
#include "core/resolved_inventory.h"

#include <libudev.h>

#include <algorithm>
#include <cerrno>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <tuple>
#include <vector>

using namespace drivelab;

struct FakeValue {
    std::optional<std::string> text;
    int error = 0;
};
struct udev {};
struct udev_enumerate {};
struct udev_list_entry {
    std::string path;
    bool unnamed = false;
    udev_list_entry* next = nullptr;
};
struct udev_device {
    std::string name;
    std::string path;
    std::string devnode;
    std::string type = "disk";
    std::map<std::string, FakeValue> properties;
    std::map<std::string, FakeValue> attributes;
    udev_device* parent = nullptr;
    bool nvme_parent = false;
    int open_error = 0;
    int after_read_error = 0;
    int opens = 0;
};

namespace {
struct FakeUdev {
    int init_error = 0;
    int allocation_error = 0;
    int match_error = 0;
    int scan_error = 0;
    int list_error = 0;
    int contexts = 0;
    int enumerations = 0;
    int references = 0;
    std::map<std::string, std::unique_ptr<udev_device>> devices;
    std::vector<udev_list_entry> entries;
    std::vector<std::pair<std::string, std::string>> attribute_reads;
} fake;

void reset() {
    DL_CHECK(fake.contexts == 0 && fake.enumerations == 0 && fake.references == 0);
    fake = {};
}

udev_device& addDevice(std::string name, std::string major, std::string minor) {
    auto value = std::make_unique<udev_device>();
    value->name = name;
    value->devnode = "/dev/" + name;
    value->path = "/sys/devices/pci0000:00/block/" + name;
    value->properties["MAJOR"] = {std::move(major), 0};
    value->properties["MINOR"] = {std::move(minor), 0};
    fake.entries.push_back({value->path});
    auto* pointer = value.get();
    fake.devices.emplace(value->path, std::move(value));
    return *pointer;
}

const char* lookup(const std::map<std::string, FakeValue>& values, const char* name) {
    const auto it = values.find(name);
    if (it == values.end()) {
        errno = ENOENT;
        return nullptr;
    }
    errno = it->second.error;
    return it->second.text ? it->second.text->c_str() : nullptr;
}

BlockInventorySnapshot scan() {
    UdevSysfsInventorySource source;
    LinuxInventoryProvider provider(source);
    auto result = provider.scan();
    DL_CHECK(result);
    DL_CHECK(fake.contexts == 0 && fake.enumerations == 0 && fake.references == 0);
    return result.value();
}

// Return a test-owned copy: neither a temporary lookup name nor a destroyed
// snapshot can invalidate the observation retained by an assertion.
BlockDeviceObservation device(const BlockInventorySnapshot& snapshot, std::string_view name) {
    const auto it = std::find_if(snapshot.devices.begin(), snapshot.devices.end(),
        [&](const auto& value) { return value.kernel_name == name; });
    DL_CHECK(it != snapshot.devices.end());
    return *it;
}

bool issue(const std::vector<BlockInventoryIssue>& issues, BlockInventoryIssueCode code,
           const std::string& field, std::optional<int> error = std::nullopt) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& value) {
        return value.code == code && value.field == field &&
               (!error || value.native_error == error);
    });
}


void testNativeDeviceKinds() {
    reset();
    auto& optical = addDevice("ordinary-name", "11", "0");
    optical.attributes["device/type"] = {"5\n",0};
    optical.properties["ID_TYPE"] = {"cd",0};
    optical.properties["ID_CDROM"] = {"1",0};
    optical.properties["ID_BUS"] = {"ata",0};
    auto& disk = addDevice("sr-lookalike", "8", "0");
    disk.attributes["device/type"] = {"0",0};
    auto& nvme = addDevice("namespace-without-prefix", "259", "0");
    nvme.nvme_parent = true;
    auto& floppy = addDevice("removable-bridge", "8", "16");
    floppy.attributes["device/type"] = {"0",0};
    floppy.properties["ID_USB_TYPE"] = {"floppy",0};
    addDevice("sda-looking-but-unproven", "8", "32");
    auto snapshot = scan();
    DL_CHECK(device(snapshot,"ordinary-name").physical_kind == PhysicalDeviceKind::Optical);
    DL_CHECK(device(snapshot,"ordinary-name").transport == "ata");
    DL_CHECK(device(snapshot,"sr-lookalike").physical_kind == PhysicalDeviceKind::Disk);
    DL_CHECK(device(snapshot,"namespace-without-prefix").physical_kind == PhysicalDeviceKind::Disk);
    DL_CHECK(device(snapshot,"removable-bridge").physical_kind == PhysicalDeviceKind::Floppy);
    DL_CHECK(device(snapshot,"sda-looking-but-unproven").physical_kind == PhysicalDeviceKind::UnknownPhysical);
    optical.attributes["device/type"] = {"0",0};
    DL_CHECK(device(scan(),"ordinary-name").physical_kind == PhysicalDeviceKind::UnknownPhysical);
    disk.properties["ID_TYPE"] = {std::nullopt,EACCES};
    DL_CHECK(device(scan(),"sr-lookalike").physical_kind == PhysicalDeviceKind::UnknownPhysical);
}

void testDeviceLookupLifetime() {
    reset();
    addDevice("owned", "8", "0").properties["ID_SERIAL_SHORT"] = {"retained", 0};
    auto snapshot = scan();
    // These references extend the lifetimes of returned values, not snapshot
    // elements. Clearing the source snapshot must not invalidate either result.
    const auto& retained = device(snapshot, std::string("owned"));
    const auto& from_temporary = device(scan(), std::string("owned"));
    snapshot.devices.clear();
    reset();
    DL_CHECK(retained.kernel_name == "owned" && retained.serial == "retained");
    DL_CHECK(from_temporary.kernel_name == "owned" && from_temporary.serial == "retained");
}

void testNativeFactsAndNamespacePaths() {
    reset();
    auto& sata = addDevice("sda", "8", "0");
    sata.attributes["size"] = {"1250263728\n", 0};
    sata.attributes["device/vendor"] = {" ATA ", 0};
    sata.properties["ID_VENDOR"] = {"ATA", 0};
    sata.attributes["device/model"] = {"SATA fixture", 0};
    sata.attributes["queue/rotational"] = {"1", 0};
    sata.attributes["removable"] = {"0", 0};
    sata.attributes["ro"] = {"0", 0};
    sata.properties["ID_SERIAL_SHORT"] = {"SATA-SERIAL", 0};
    sata.properties["ID_WWN"] = {"0x50014ee001234567", 0};
    sata.properties["ID_BUS"] = {"ata", 0};
    auto& sas = addDevice("sdb", "8", "16");
    sas.properties["ID_BUS"] = {"scsi", 0};
    sas.properties["ID_SERIAL_SHORT"] = {"SAS-SERIAL", 0};
    sas.attributes["device/model"] = {"SAS fixture", 0};

    // A name is never transport/origin evidence.
    auto& nvme = addDevice("nvme0n1", "259", "0");
    nvme.attributes["eui"] = {"00 25 38 b9 12 34 56 78", 0};
    nvme.attributes["nguid"] = {"00112233-4455-6677-8899-aabbccddeeff", 0};
    nvme.attributes["device/model"] = {"NVMe fixture", 0};
    nvme.attributes["device/serial"] = {"NVME-SERIAL", 0};
    nvme.attributes["wwid"] = {"eui.00112233445566778899aabbccddeeff", 0};
    nvme.attributes["device/wwid"] = {"synthetic-do-not-promote", 0};
    nvme.attributes["device/eui"] = {"incorrect-controller-value", 0};

    auto& partition = addDevice("sda1", "8", "1");
    partition.type = "partition";
    partition.parent = &sata;
    partition.attributes["partition"] = {"1", 0};
    partition.attributes["size"] = {"100", 0};

    const auto snapshot = scan();
    const auto disk = device(snapshot, "sda");
    DL_CHECK(disk.capacity_bytes == 640135028736ULL);
    DL_CHECK(disk.sysfs_path == sata.path);
    DL_CHECK(disk.vendor == "ATA" && disk.model == "SATA fixture");
    DL_CHECK(disk.wwn == "0x50014ee001234567");
    DL_CHECK(disk.observation_kind == BlockObservationKind::Unknown);
    DL_CHECK(disk.child_kernel_names == std::vector<std::string>{"sda1"});
    DL_CHECK(device(snapshot, "sda1").parent_kernel_name == "sda");
    DL_CHECK(!device(snapshot, "sda1").read_only);
    DL_CHECK(device(snapshot, "sdb").serial == "SAS-SERIAL");
    DL_CHECK(device(snapshot, "sdb").transport == "scsi");
    const auto namespace_device = device(snapshot, "nvme0n1");
    DL_CHECK(namespace_device.nvme_eui == "00 25 38 b9 12 34 56 78");
    DL_CHECK(namespace_device.nvme_nguid == "00112233-4455-6677-8899-aabbccddeeff");
    DL_CHECK(!namespace_device.wwn && !namespace_device.transport);
    DL_CHECK(namespace_device.serial == "NVME-SERIAL");
    for (const auto& [path, attribute] : fake.attribute_reads) {
        (void)path;
        DL_CHECK(attribute != "device/eui" && attribute != "device/nguid");
        DL_CHECK(attribute != "wwid" && attribute != "device/wwid");
    }
}

void testReadErrorsAndConflicts() {
    reset();
    auto& disk = addDevice("sdc", "8", "32");
    disk.attributes["size"] = {std::nullopt, EACCES};
    disk.attributes["device/model"] = {std::nullopt, EIO};
    disk.properties["ID_MODEL"] = {"plausible fallback", 0};
    disk.attributes["device/serial"] = {"SERIAL-A", 0};
    disk.properties["ID_SERIAL_SHORT"] = {"SERIAL-B", 0};
    disk.attributes["removable"] = {"1", 0};
    disk.attributes["ro"] = {"1", 0};
    disk.properties["ID_BUS"] = {std::nullopt, EACCES};
    const auto snapshot = scan();
    const auto observed = device(snapshot, "sdc");
    DL_CHECK(!observed.capacity_bytes && !observed.model && !observed.serial);
    DL_CHECK(!observed.transport && !observed.nvme_eui && !observed.nvme_nguid);
    DL_CHECK(observed.removable == true && observed.read_only == true);
    DL_CHECK(issue(observed.issues, BlockInventoryIssueCode::SourceReadFailure,
                   "capacity_sectors_512", EACCES));
    DL_CHECK(issue(observed.issues, BlockInventoryIssueCode::SourceReadFailure, "model", EIO));
    DL_CHECK(issue(observed.issues, BlockInventoryIssueCode::ConflictingAttribute, "serial"));
    DL_CHECK(!issue(observed.issues, BlockInventoryIssueCode::SourceReadFailure, "nvme_eui"));

    // Missing one major/minor property must not overwrite the other observation.
    disk.properties.erase("MINOR");
    DL_CHECK(!device(scan(), "sdc").device_number);
}

void testAtaModelEquivalence() {
    for (const auto& [sysfs, udev, encoded] : std::vector<std::tuple<std::string, std::string, std::string>>{
        {"ST2000DM001-1E61", "ST2000DM001-1E6164", "ST2000DM001-1E6164"},
        {"KINGSTON SV300S3", "KINGSTON_SV300S37A240G", "KINGSTON\\x20SV300S37A240G"},
        {"ATA  Model", "ATA_Model", "ATA\\x20\\x20Model"},
        {"ATA_Model", "ATA_Model", "ATA_Model"}}) {
        reset();
        auto& disk = addDevice("sda", "8", "0");
        disk.attributes["device/model"] = {sysfs, 0};
        disk.properties["ID_MODEL"] = {udev, 0};
        disk.properties["ID_MODEL_ENC"] = {encoded, 0};
        disk.properties["ID_ATA"] = {"1", 0};
        disk.properties["ID_BUS"] = {"ata", 0};
        disk.properties["ID_WWN"] = {"5000c500644b185f", 0};
        auto snapshot = scan();
        auto resolved = resolveBlockIdentities(snapshot);
        DL_CHECK(resolved.nodes[0].identity.id->value == "prod:wwn:5000c500644b185f");
        const auto observations = snapshot.devices[0].model_observations;
        DL_CHECK(std::find(observations.begin(), observations.end(),
            BlockModelObservation{"sysfs:device/model", sysfs}) != observations.end());
        DL_CHECK(std::find(observations.begin(), observations.end(),
            BlockModelObservation{"udev:ID_MODEL", udev}) != observations.end());
        DL_CHECK(std::find(observations.begin(), observations.end(),
            BlockModelObservation{"udev:ID_MODEL_ENC", encoded}) != observations.end());
        auto rejects = [&] {
            const auto raw = scan();
            DL_CHECK(issue(raw.devices[0].issues, BlockInventoryIssueCode::ConflictingAttribute, "model"));
            DL_CHECK(!resolveBlockIdentities(raw).nodes[0].identity.id);
        };
        disk.attributes["device/model"] = {"OTHER MODEL", 0}; rejects();
        disk.attributes["device/model"] = {"ST2000", 0}; rejects(); // Arbitrary short prefixes fail.
        disk.attributes["device/model"] = {sysfs, 0};
        if (sysfs != udev) {
            disk.properties.erase("ID_MODEL_ENC"); rejects();
            disk.properties["ID_MODEL_ENC"] = {encoded, 0};
            disk.properties["ID_BUS"] = {"usb", 0}; rejects();
            disk.properties["ID_BUS"] = {"ata", 0};
            disk.properties["ID_ATA"] = {"0", 0}; rejects();
            disk.properties["ID_ATA"] = {"1", 0};
            disk.properties["ID_MODEL_ENC"] = {"Malformed\\xGG", 0}; rejects();
            disk.properties["ID_MODEL_ENC"] = {encoded, 0};
        }
        disk.attributes["device/serial"] = {"A", 0};
        disk.properties["ID_SERIAL_SHORT"] = {"B", 0};
        DL_CHECK(!resolveBlockIdentities(scan()).nodes[0].identity.id);
        disk.attributes.erase("device/serial"); disk.properties.erase("ID_SERIAL_SHORT");
        auto& duplicate = addDevice("sdb", "8", "16");
        duplicate.properties["ID_WWN"] = {"5000c500644b185f", 0};
        resolved = resolveBlockIdentities(scan());
        for (const auto& n : resolved.nodes) DL_CHECK(n.identity.state == ResolutionState::Ambiguous);
        std::reverse(fake.entries.begin(), fake.entries.end());
        DL_CHECK(resolveBlockIdentities(scan()) == resolved);
    }
    // '_' in the encoded original is literal: never reverse it into a space.
    reset();
    auto& disk = addDevice("sda", "8", "0");
    disk.attributes["device/model"] = {"KINGSTON SV300S3", 0};
    disk.properties["ID_MODEL"] = {"KINGSTON_SV300S37A240G", 0};
    disk.properties["ID_MODEL_ENC"] = {"KINGSTON_SV300S37A240G", 0};
    disk.properties["ID_ATA"] = {"1", 0}; disk.properties["ID_BUS"] = {"ata", 0};
    DL_CHECK(!device(scan(), "sda").model);
}

void testDisappearanceAndReadFailureIsolation() {
    reset();
    addDevice("good", "8", "0");
    addDevice("gone", "8", "1").open_error = ENOENT;
    addDevice("denied", "8", "2").open_error = EACCES;
    addDevice("midscan", "8", "3").after_read_error = ENOENT;
    addDevice("attribute-gone", "8", "4").attributes["size"] = {std::nullopt, ENODEV};
    fake.entries.push_back({"", true});
    const auto snapshot = scan();
    DL_CHECK(snapshot.devices.size() == 1 && snapshot.devices.front().kernel_name == "good");
    DL_CHECK(issue(snapshot.issues, BlockInventoryIssueCode::DeviceDisappeared, "sysfs_path", ENOENT));
    DL_CHECK(issue(snapshot.issues, BlockInventoryIssueCode::SourceReadFailure, "sysfs_path", EACCES));
    DL_CHECK(issue(snapshot.issues, BlockInventoryIssueCode::DeviceDisappeared,
                   "capacity_sectors_512", ENODEV));
}

void testFatalFailuresAndEmptyEnumeration() {
    for (int point = 0; point != 5; ++point) {
        reset();
        if (point == 0) fake.init_error = ENOMEM;
        if (point == 1) fake.allocation_error = ENOMEM;
        if (point == 2) fake.match_error = EACCES;
        if (point == 3) fake.scan_error = EIO;
        if (point == 4) fake.list_error = EIO;
        UdevSysfsInventorySource source;
        auto result = source.scan();
        DL_CHECK(!result);
        const auto expected = point < 2 ? ErrorCode::Unavailable
            : point == 2 ? ErrorCode::PermissionDenied : ErrorCode::IoError;
        DL_CHECK(result.error().code == expected);
        DL_CHECK(result.error().component == "UdevSysfsInventorySource");
        DL_CHECK(fake.contexts == 0 && fake.enumerations == 0 && fake.references == 0);
    }
    reset();
    DL_CHECK(scan().devices.empty());
}

void testNativeDuplicateOrderAndVirtualEvidence() {
    reset();
    auto& disk = addDevice("not-a-prefix-rule", "7", "0");
    const std::string old_path = disk.path;
    auto owned = std::move(fake.devices.at(old_path));
    fake.devices.erase(old_path);
    owned->path = "/sys/devices/virtual/block/not-a-prefix-rule";
    fake.entries.front().path = owned->path;
    fake.devices.emplace(owned->path, std::move(owned));
    fake.entries.push_back(fake.entries.front());
    addDevice("loop-looking-physical-path", "7", "1");
    const auto first = scan();
    DL_CHECK(device(first, "not-a-prefix-rule").observation_kind ==
             BlockObservationKind::VirtualOrPseudo);
    DL_CHECK(device(first, "loop-looking-physical-path").observation_kind ==
             BlockObservationKind::Unknown);
    std::reverse(fake.entries.begin(), fake.entries.end());
    DL_CHECK(scan() == first);
}
}  // namespace

extern "C" {
udev* udev_new() {
    if (fake.init_error) { errno = fake.init_error; return nullptr; }
    ++fake.contexts;
    return new udev;
}
udev* udev_unref(udev* value) { --fake.contexts; delete value; return nullptr; }
udev_enumerate* udev_enumerate_new(udev*) {
    if (fake.allocation_error) { errno = fake.allocation_error; return nullptr; }
    ++fake.enumerations;
    return new udev_enumerate;
}
udev_enumerate* udev_enumerate_unref(udev_enumerate* value) {
    --fake.enumerations; delete value; return nullptr;
}
int udev_enumerate_add_match_subsystem(udev_enumerate*, const char* subsystem) {
    DL_CHECK(std::string(subsystem) == "block");
    return -fake.match_error;
}
int udev_enumerate_scan_devices(udev_enumerate*) { return -fake.scan_error; }
udev_list_entry* udev_enumerate_get_list_entry(udev_enumerate*) {
    if (fake.list_error) { errno = fake.list_error; return nullptr; }
    for (std::size_t i = 0; i < fake.entries.size(); ++i) {
        fake.entries[i].next = i + 1 < fake.entries.size() ? &fake.entries[i + 1] : nullptr;
    }
    return fake.entries.empty() ? nullptr : &fake.entries.front();
}
udev_list_entry* udev_list_entry_get_next(udev_list_entry* value) { return value->next; }
const char* udev_list_entry_get_name(udev_list_entry* value) {
    return value->unnamed ? nullptr : value->path.c_str();
}
udev_device* udev_device_new_from_syspath(udev*, const char* path) {
    const auto it = fake.devices.find(path);
    if (it == fake.devices.end()) { errno = ENOENT; return nullptr; }
    auto* value = it->second.get();
    ++value->opens;
    const int error = value->open_error ? value->open_error
        : value->opens > 1 ? value->after_read_error : 0;
    if (error) { errno = error; return nullptr; }
    ++fake.references;
    return value;
}
udev_device* udev_device_unref(udev_device*) { --fake.references; return nullptr; }
const char* udev_device_get_sysname(udev_device* value) { return value->name.c_str(); }
const char* udev_device_get_syspath(udev_device* value) { return value->path.c_str(); }
const char* udev_device_get_devnode(udev_device* value) { return value->devnode.c_str(); }
const char* udev_device_get_devtype(udev_device* value) { return value->type.c_str(); }
const char* udev_device_get_property_value(udev_device* value, const char* name) {
    return lookup(value->properties, name);
}
const char* udev_device_get_sysattr_value(udev_device* value, const char* name) {
    fake.attribute_reads.emplace_back(value->path, name);
    return lookup(value->attributes, name);
}
udev_device* udev_device_get_parent_with_subsystem_devtype(
    udev_device* value, const char* subsystem, const char* type) {
    if (std::string(subsystem) == "scsi") {
        DL_CHECK(type == nullptr);
        errno = value->attributes.contains("device/type") ? 0 : ENOENT;
        return value->attributes.contains("device/type") ? value : nullptr;
    }
    if (std::string(subsystem) == "nvme") {
        DL_CHECK(type == nullptr);
        errno = value->nvme_parent ? 0 : ENOENT;
        return value->nvme_parent ? value : nullptr;
    }
    DL_CHECK(std::string(subsystem) == "block" && std::string(type) == "disk");
    if (!value->parent) errno = ENOENT;
    return value->parent;
}
}

int main() {
    return test::run([] {
        testNativeDeviceKinds();
        testDeviceLookupLifetime();
        testNativeFactsAndNamespacePaths();
        testReadErrorsAndConflicts();
        testAtaModelEquivalence();
        testDisappearanceAndReadFailureIsolation();
        testFatalFailuresAndEmptyEnumeration();
        testNativeDuplicateOrderAndVirtualEvidence();
        reset();
    });
}
