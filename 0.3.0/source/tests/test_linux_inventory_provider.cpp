#include "test_support.h"

#include "platform/linux/linux_inventory_provider.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace drivelab;

static_assert(std::is_base_of_v<BlockInventoryProvider, LinuxInventoryProvider>);
static_assert(!std::is_base_of_v<InventoryProvider, LinuxInventoryProvider>);
static_assert(std::is_same_v<
              decltype(std::declval<BlockInventoryProvider&>().scan()),
              Result<BlockInventorySnapshot>>);

namespace {

class FakeLinuxInventorySource final : public LinuxInventorySource {
public:
    LinuxInventorySourceSnapshot snapshot;
    std::optional<Error> fatal_error;
    int scan_count = 0;

    Result<LinuxInventorySourceSnapshot> scan() override {
        ++scan_count;
        if (fatal_error) {
            return Result<LinuxInventorySourceSnapshot>::failure(*fatal_error);
        }
        return Result<LinuxInventorySourceSnapshot>::success(snapshot);
    }
};

LinuxBlockSourceRecord diskRecord(std::string kernel_name,
                                  std::string major_number,
                                  std::string minor_number) {
    LinuxBlockSourceRecord record;
    record.kernel_name = std::move(kernel_name);
    record.node_type = "disk";
    record.major_number = std::move(major_number);
    record.minor_number = std::move(minor_number);
    return record;
}

LinuxBlockSourceRecord partitionRecord(std::string kernel_name,
                                       std::string major_number,
                                       std::string minor_number,
                                       std::string parent,
                                       std::string partition_number) {
    LinuxBlockSourceRecord record = diskRecord(
        std::move(kernel_name), std::move(major_number), std::move(minor_number));
    record.node_type = "partition";
    record.parent_kernel_name = std::move(parent);
    record.partition_number = std::move(partition_number);
    return record;
}

BlockInventorySnapshot scanRecords(
    std::vector<LinuxBlockSourceRecord> records,
    std::vector<BlockInventoryIssue> issues = {}) {
    FakeLinuxInventorySource source;
    source.snapshot.records = std::move(records);
    source.snapshot.issues = std::move(issues);
    LinuxInventoryProvider provider(source);
    Result<BlockInventorySnapshot> result = provider.scan();
    DL_CHECK(result);
    DL_CHECK(source.scan_count == 1);
    return result.value();
}

const BlockDeviceObservation* findDevice(const BlockInventorySnapshot& snapshot,
                                         std::string_view kernel_name) {
    const auto match = std::find_if(
        snapshot.devices.begin(), snapshot.devices.end(),
        [&](const BlockDeviceObservation& device) {
            return device.kernel_name == kernel_name;
        });
    return match == snapshot.devices.end() ? nullptr : &*match;
}

bool hasIssue(const std::vector<BlockInventoryIssue>& issues,
              BlockInventoryIssueCode code,
              std::string_view field,
              std::optional<std::string_view> kernel_name = std::nullopt) {
    return std::any_of(issues.begin(), issues.end(), [&](const BlockInventoryIssue& issue) {
        if (issue.code != code || issue.field != field) return false;
        if (!kernel_name) return true;
        return issue.kernel_name && *issue.kernel_name == *kernel_name;
    });
}

std::vector<std::string> deviceNames(const BlockInventorySnapshot& snapshot) {
    std::vector<std::string> names;
    for (const BlockDeviceObservation& device : snapshot.devices) {
        names.push_back(device.kernel_name);
    }
    return names;
}


void testPhysicalKinds() {
    using Kind = PhysicalDeviceKind;
    auto record = diskRecord("arbitrary-name", "8", "0");
    for (const std::string bus : {"ata", "sata", "usb", "nvme"}) {
        record.transport = bus;
        record.device_kind_observations = {{"udev:ID_TYPE","disk"}};
        const auto device = scanRecords({record}).devices.front();
        DL_CHECK(device.physical_kind == Kind::Disk && device.transport == bus);
    }
    for (const auto& facts : std::vector<std::vector<DeviceKindObservation>>{
        {{"sysfs:parent/subsystem","nvme"}}, {{"sysfs:scsi/type","0"}},
        {{"sysfs:scsi/type","14"}}, {{"sysfs:scsi/type","20"}}}) {
        record.device_kind_observations = facts;
        DL_CHECK(scanRecords({record}).devices.front().physical_kind == Kind::Disk);
    }
    for (const auto& facts : std::vector<std::vector<DeviceKindObservation>>{
        {{"udev:ID_TYPE","cd"}}, {{"udev:ID_CDROM","1"}}, {{"sysfs:scsi/type","5"}},
        {{"sysfs:scsi/type","4"}}, {{"sysfs:scsi/type","7"}}, {{"sysfs:scsi/type","15"}}}) {
        record.kernel_name = "looks-like-a-hard-disk";
        record.device_kind_observations = facts;
        DL_CHECK(scanRecords({record}).devices.front().physical_kind == Kind::Optical);
    }
    for (const auto& facts : std::vector<std::vector<DeviceKindObservation>>{
        {{"udev:ID_DRIVE_FLOPPY","1"}}, {{"udev:ID_USB_TYPE","floppy"},{"sysfs:scsi/type","0"}}}) {
        record.device_kind_observations = facts;
        DL_CHECK(scanRecords({record}).devices.front().physical_kind == Kind::Floppy);
    }
    for (const auto& facts : std::vector<std::vector<DeviceKindObservation>>{
        {}, {{"sysfs:scsi/type","not-a-number"}}, {{"sysfs:scsi/type","1"}},
        {{"udev:ID_TYPE","disk"},{"udev:ID_CDROM","1"}},
        {{"sysfs:parent/subsystem","nvme"},{"udev:ID_DRIVE_FLOPPY","1"}}}) {
        record.kernel_name = "sr0"; // Names and NVMe transport never establish kind.
        record.device_kind_observations = facts;
        DL_CHECK(scanRecords({record}).devices.front().physical_kind == Kind::UnknownPhysical);
    }
    record.device_kind_observations = {{"udev:ID_TYPE","disk"}};
    auto conflicting = record;
    conflicting.device_kind_observations = {{"udev:ID_TYPE","cd"}};
    const auto result = scanRecords({record,conflicting});
    DL_CHECK(result.devices.front().physical_kind == Kind::UnknownPhysical);
    DL_CHECK(scanRecords({conflicting,record}) == result);
}

void testSataFactMapping() {
    LinuxBlockSourceRecord sata = diskRecord("sda", "8", "0");
    sata.current_path = "/dev/sda";
    sata.sysfs_path = "/sys/devices/pci0000:00/host0/block/sda";
    sata.capacity_sectors_512 = "1250263728";
    sata.vendor = " ATA ";
    sata.model = "WDC WD6400AAKS-75A7B0";
    sata.serial = "WD-WMASY1520445";
    sata.wwn = "0x50014ee001234567";
    sata.rotational = "1";
    sata.removable = "0";
    sata.read_only = "0";
    sata.transport = "ata";
    sata.observation_kind = BlockObservationKind::Unknown;

    const BlockInventorySnapshot snapshot = scanRecords({sata});
    DL_CHECK(snapshot.devices.size() == 1);
    const BlockDeviceObservation& device = snapshot.devices.front();
    DL_CHECK(device.kernel_name == "sda");
    DL_CHECK(device.current_path && *device.current_path == "/dev/sda");
    DL_CHECK(device.sysfs_path == sata.sysfs_path);
    DL_CHECK(device.node_type == BlockNodeType::Disk);
    DL_CHECK(device.device_number);
    DL_CHECK(device.device_number->major_number == 8);
    DL_CHECK(device.device_number->minor_number == 0);
    DL_CHECK(device.capacity_bytes && *device.capacity_bytes == 640135028736ULL);
    DL_CHECK(device.vendor && *device.vendor == "ATA");
    DL_CHECK(device.model && *device.model == "WDC WD6400AAKS-75A7B0");
    DL_CHECK(device.serial && *device.serial == "WD-WMASY1520445");
    DL_CHECK(device.wwn && *device.wwn == "0x50014ee001234567");
    DL_CHECK(device.rotational && *device.rotational);
    DL_CHECK(device.removable && !*device.removable);
    DL_CHECK(device.read_only && !*device.read_only);
    DL_CHECK(device.transport && *device.transport == "ata");
    DL_CHECK(device.observation_kind == BlockObservationKind::Unknown);
    DL_CHECK(device.issues.empty());
}

void testMissingAndBlankFacts() {
    LinuxBlockSourceRecord missing = diskRecord("nvme9n1", "259", "9");
    const BlockInventorySnapshot missing_snapshot = scanRecords({missing});
    const BlockDeviceObservation& device = missing_snapshot.devices.front();
    DL_CHECK(!device.current_path);
    DL_CHECK(!device.sysfs_path);
    DL_CHECK(!device.capacity_bytes);
    DL_CHECK(!device.vendor);
    DL_CHECK(!device.model);
    DL_CHECK(!device.serial);
    DL_CHECK(!device.wwn);
    DL_CHECK(!device.nvme_eui);
    DL_CHECK(!device.nvme_nguid);
    DL_CHECK(!device.rotational);
    DL_CHECK(!device.removable);
    DL_CHECK(!device.read_only);
    DL_CHECK(!device.transport);
    DL_CHECK(!device.parent_kernel_name);
    DL_CHECK(!device.partition_number);
    DL_CHECK(device.observation_kind == BlockObservationKind::Unknown);

    LinuxBlockSourceRecord blank = diskRecord("sdb", "8", "16");
    blank.serial = "   ";
    blank.wwn = "";
    blank.nvme_eui = "\t";
    blank.nvme_nguid = "  \n";
    const BlockInventorySnapshot blank_snapshot = scanRecords({blank});
    const BlockDeviceObservation& blank_device = blank_snapshot.devices.front();
    DL_CHECK(!blank_device.serial);
    DL_CHECK(!blank_device.wwn);
    DL_CHECK(!blank_device.nvme_eui);
    DL_CHECK(!blank_device.nvme_nguid);
    DL_CHECK(hasIssue(blank_device.issues, BlockInventoryIssueCode::MalformedAttribute,
                      "serial", "sdb"));
    DL_CHECK(hasIssue(blank_device.issues, BlockInventoryIssueCode::MalformedAttribute,
                      "wwn", "sdb"));
}

void testMalformedAttributes() {
    LinuxBlockSourceRecord malformed = diskRecord("bad0", "8x", "-1");
    malformed.node_type = "volume";
    malformed.capacity_sectors_512 = "12 sectors";
    malformed.rotational = "true";
    malformed.removable = "2";
    malformed.read_only = "";
    malformed.partition_number = "one";

    const BlockInventorySnapshot snapshot = scanRecords({malformed});
    const BlockDeviceObservation& device = snapshot.devices.front();
    DL_CHECK(device.node_type == BlockNodeType::Unknown);
    DL_CHECK(!device.device_number);
    DL_CHECK(!device.capacity_bytes);
    DL_CHECK(!device.rotational);
    DL_CHECK(!device.removable);
    DL_CHECK(!device.read_only);
    DL_CHECK(!device.partition_number);
    for (std::string_view field : {"node_type", "major_number", "minor_number",
                                   "capacity_sectors_512", "rotational", "removable",
                                   "read_only", "partition_number"}) {
        DL_CHECK(hasIssue(device.issues, BlockInventoryIssueCode::MalformedAttribute,
                          field, "bad0"));
    }
}

void testCapacityBoundaries() {
    LinuxBlockSourceRecord zero = diskRecord("cap0", "1", "0");
    zero.capacity_sectors_512 = "0";
    LinuxBlockSourceRecord one = diskRecord("cap1", "1", "1");
    one.capacity_sectors_512 = "1";
    LinuxBlockSourceRecord maximum = diskRecord("capmax", "1", "2");
    maximum.capacity_sectors_512 = "36028797018963967";
    LinuxBlockSourceRecord overflow = diskRecord("overflow", "1", "3");
    overflow.capacity_sectors_512 = "36028797018963968";

    const BlockInventorySnapshot snapshot = scanRecords({overflow, maximum, one, zero});
    DL_CHECK(*findDevice(snapshot, "cap0")->capacity_bytes == 0ULL);
    DL_CHECK(*findDevice(snapshot, "cap1")->capacity_bytes == 512ULL);
    DL_CHECK(*findDevice(snapshot, "capmax")->capacity_bytes ==
             18446744073709551104ULL);
    const BlockDeviceObservation* overflow_device = findDevice(snapshot, "overflow");
    DL_CHECK(overflow_device != nullptr);
    DL_CHECK(!overflow_device->capacity_bytes);
    DL_CHECK(hasIssue(overflow_device->issues, BlockInventoryIssueCode::CapacityOverflow,
                      "capacity_sectors_512", "overflow"));
}

void testDiskPartitionRelationships() {
    LinuxBlockSourceRecord disk = diskRecord("sda", "8", "0");
    LinuxBlockSourceRecord first = partitionRecord("sda1", "8", "1", "sda", "1");
    LinuxBlockSourceRecord second = partitionRecord("sda2", "8", "2", "sda", "2");
    LinuxBlockSourceRecord orphan = partitionRecord("sdb1", "8", "17", "sdb", "1");

    const BlockInventorySnapshot snapshot = scanRecords({second, orphan, first, disk});
    DL_CHECK(deviceNames(snapshot) ==
             std::vector<std::string>({"sda", "sda1", "sda2", "sdb1"}));
    const BlockDeviceObservation* disk_device = findDevice(snapshot, "sda");
    DL_CHECK(disk_device != nullptr);
    DL_CHECK(disk_device->child_kernel_names ==
             std::vector<std::string>({"sda1", "sda2"}));
    DL_CHECK(findDevice(snapshot, "sda1")->parent_kernel_name ==
             std::optional<std::string>("sda"));
    DL_CHECK(findDevice(snapshot, "sda2")->partition_number ==
             std::optional<std::uint32_t>(2));
    const BlockDeviceObservation* orphan_device = findDevice(snapshot, "sdb1");
    DL_CHECK(orphan_device != nullptr);
    DL_CHECK(orphan_device->parent_kernel_name == std::optional<std::string>("sdb"));
    DL_CHECK(hasIssue(orphan_device->issues, BlockInventoryIssueCode::InvalidRelationship,
                      "parent_kernel_name", "sdb1"));
}

void testNvmeUsbAndPseudoFacts() {
    LinuxBlockSourceRecord nvme = diskRecord("nvme0n1", "259", "0");
    nvme.nvme_eui = "002538b912345678";
    nvme.nvme_nguid = "00112233445566778899aabbccddeeff";

    LinuxBlockSourceRecord usb = diskRecord("sdc", "8", "32");
    usb.transport = "usb";
    usb.rotational = "0";
    usb.removable = "1";
    usb.read_only = "1";
    usb.observation_kind = BlockObservationKind::Unknown;

    LinuxBlockSourceRecord loop = diskRecord("loop0", "7", "0");
    loop.observation_kind = BlockObservationKind::VirtualOrPseudo;
    LinuxBlockSourceRecord zram = diskRecord("zram0", "252", "0");
    zram.observation_kind = BlockObservationKind::VirtualOrPseudo;
    LinuxBlockSourceRecord mapper = diskRecord("dm-0", "253", "0");
    mapper.observation_kind = BlockObservationKind::VirtualOrPseudo;

    const BlockInventorySnapshot snapshot = scanRecords({zram, usb, nvme, loop, mapper});
    const BlockDeviceObservation* nvme_device = findDevice(snapshot, "nvme0n1");
    DL_CHECK(nvme_device != nullptr);
    DL_CHECK(nvme_device->nvme_eui ==
             std::optional<std::string>("002538b912345678"));
    DL_CHECK(nvme_device->nvme_nguid ==
             std::optional<std::string>("00112233445566778899aabbccddeeff"));
    DL_CHECK(!nvme_device->wwn);
    DL_CHECK(!nvme_device->transport);

    const BlockDeviceObservation* usb_device = findDevice(snapshot, "sdc");
    DL_CHECK(usb_device != nullptr);
    DL_CHECK(usb_device->transport == std::optional<std::string>("usb"));
    DL_CHECK(usb_device->removable && *usb_device->removable);
    DL_CHECK(usb_device->read_only && *usb_device->read_only);
    DL_CHECK(usb_device->rotational && !*usb_device->rotational);
    DL_CHECK(usb_device->observation_kind == BlockObservationKind::Unknown);

    for (std::string_view name : {"loop0", "zram0", "dm-0"}) {
        const BlockDeviceObservation* device = findDevice(snapshot, name);
        DL_CHECK(device != nullptr);
        DL_CHECK(device->observation_kind == BlockObservationKind::VirtualOrPseudo);
    }
}

void testDuplicateRecordsAndStableOrdering() {
    LinuxBlockSourceRecord disk = diskRecord("sda", "8", "0");
    disk.model = "Model A";
    disk.serial = "shared-serial";
    LinuxBlockSourceRecord conflict = disk;
    conflict.model = "Model B";
    conflict.rotational = "1";
    LinuxBlockSourceRecord conflict_again = disk;
    conflict_again.rotational = "0";
    LinuxBlockSourceRecord partition = partitionRecord("sda1", "8", "1", "sda", "1");
    LinuxBlockSourceRecord loop = diskRecord("loop0", "7", "0");
    loop.observation_kind = BlockObservationKind::VirtualOrPseudo;

    std::vector<LinuxBlockSourceRecord> records = {
        partition, disk, loop, partition, conflict, conflict_again
    };
    const BlockInventorySnapshot baseline = scanRecords(records);
    const BlockDeviceObservation* merged = findDevice(baseline, "sda");
    DL_CHECK(merged != nullptr);
    DL_CHECK(!merged->model);
    DL_CHECK(!merged->rotational);
    DL_CHECK(merged->serial == std::optional<std::string>("shared-serial"));
    DL_CHECK(merged->child_kernel_names == std::vector<std::string>({"sda1"}));
    DL_CHECK(hasIssue(merged->issues, BlockInventoryIssueCode::DuplicateRecord,
                      "kernel_name", "sda"));
    DL_CHECK(hasIssue(merged->issues, BlockInventoryIssueCode::DuplicateRecord,
                      "model", "sda"));
    DL_CHECK(hasIssue(merged->issues, BlockInventoryIssueCode::DuplicateRecord,
                      "rotational", "sda"));

    for (std::uint32_t seed = 0; seed < 32; ++seed) {
        std::vector<LinuxBlockSourceRecord> shuffled = records;
        std::mt19937 engine(seed);
        std::shuffle(shuffled.begin(), shuffled.end(), engine);
        DL_CHECK(scanRecords(std::move(shuffled)) == baseline);
    }
}

void testLocalIssuesAndFatalFailure() {
    LinuxBlockSourceRecord good = diskRecord("sda", "8", "0");
    good.issues.push_back({
        BlockInventoryIssueCode::SourceReadFailure,
        std::nullopt,
        "model",
        "Permission denied while reading an optional attribute"
    });
    std::vector<BlockInventoryIssue> source_issues = {
        {BlockInventoryIssueCode::DeviceDisappeared, "sdb", "syspath",
         "The node disappeared during enumeration"}
    };
    const BlockInventorySnapshot snapshot = scanRecords({good}, source_issues);
    DL_CHECK(snapshot.devices.size() == 1);
    DL_CHECK(hasIssue(snapshot.devices.front().issues,
                      BlockInventoryIssueCode::SourceReadFailure, "model", "sda"));
    DL_CHECK(hasIssue(snapshot.issues, BlockInventoryIssueCode::DeviceDisappeared,
                      "syspath", "sdb"));

    LinuxBlockSourceRecord unnamed = diskRecord("  ", "8", "16");
    const BlockInventorySnapshot unnamed_snapshot = scanRecords({good, unnamed});
    DL_CHECK(unnamed_snapshot.devices.size() == 1);
    DL_CHECK(hasIssue(unnamed_snapshot.issues,
                      BlockInventoryIssueCode::MissingRequiredField, "kernel_name"));

    FakeLinuxInventorySource source;
    source.fatal_error = Error{
        ErrorCode::PermissionDenied,
        "FakeLinuxInventorySource",
        "enumeration denied"
    };
    LinuxInventoryProvider provider(source);
    Result<BlockInventorySnapshot> failed = provider.scan();
    DL_CHECK(!failed);
    DL_CHECK(source.scan_count == 1);
    DL_CHECK(failed.error().code == ErrorCode::PermissionDenied);
    DL_CHECK(failed.error().component == "FakeLinuxInventorySource");
    DL_CHECK(failed.error().message == "enumeration denied");
}

void testSasAndOpaquePaths() {
    auto sas = diskRecord("unusual-name", "8", "80");
    sas.vendor = "SEAGATE";
    sas.model = "SAS fixture";
    sas.serial = "SAS-SERIAL";
    sas.transport = "scsi";
    sas.current_path = "/dev/unusual-name";
    sas.sysfs_path = "/sys/devices/pci0000:00/host8/block/unusual-name";
    sas.capacity_sectors_512 = " 1234\n";
    auto snapshot = scanRecords({sas});
    DL_CHECK(snapshot.devices.front().capacity_bytes == 631808ULL);
    DL_CHECK(snapshot.devices.front().transport == "scsi");
    DL_CHECK(snapshot.devices.front().observation_kind == BlockObservationKind::Unknown);
    sas.current_path = "/dev/../sda";
    sas.sysfs_path = "relative/path";
    snapshot = scanRecords({sas});
    DL_CHECK(!snapshot.devices.front().current_path);
    DL_CHECK(!snapshot.devices.front().sysfs_path);
}

void testNumericAndIdentifierEdges() {
    for (const std::string value : {"-1", "+1", "1.0", "1x", "", "0x10", "1 2",
                                    "18446744073709551616x"}) {
        auto record = diskRecord("bad", "4294967296", "0");
        record.capacity_sectors_512 = value;
        const auto snapshot = scanRecords({record});
        DL_CHECK(!snapshot.devices.front().capacity_bytes);
        DL_CHECK(!snapshot.devices.front().device_number);
        DL_CHECK(hasIssue(snapshot.devices.front().issues,
                          BlockInventoryIssueCode::MalformedAttribute, "capacity_sectors_512"));
    }
    auto record = partitionRecord("p1", "1", "1", "disk", "0");
    record.capacity_sectors_512 = "18446744073709551616";
    record.nvme_eui = "00112233445566778899aabbccddeeff";
    record.nvme_nguid = "002538b912345678";
    auto snapshot = scanRecords({record});
    const auto& device = snapshot.devices.front();
    DL_CHECK(!device.partition_number);
    DL_CHECK(!device.nvme_eui && !device.nvme_nguid);
    DL_CHECK(hasIssue(device.issues, BlockInventoryIssueCode::CapacityOverflow,
                      "capacity_sectors_512"));
    record.nvme_eui = "00 25 38 b9 12 34 56 78";
    record.nvme_nguid = "00112233-4455-6677-8899-aabbccddeeff";
    snapshot = scanRecords({record});
    DL_CHECK(snapshot.devices.front().nvme_eui == record.nvme_eui);
    DL_CHECK(snapshot.devices.front().nvme_nguid == record.nvme_nguid);
    record.nvme_eui = "00-25-38-b9-12-34-56-78";
    record.nvme_nguid = "00112233_4455-6677-8899-aabbccddeeff";
    snapshot = scanRecords({record});
    DL_CHECK(!snapshot.devices.front().nvme_eui && !snapshot.devices.front().nvme_nguid);
    record.nvme_eui = "0000000000000000";
    record.nvme_nguid = "g0112233445566778899aabbccddeeff0";
    snapshot = scanRecords({record});
    DL_CHECK(!snapshot.devices.front().nvme_eui && !snapshot.devices.front().nvme_nguid);
}

void testIncompleteDuplicatesAndLocatorCollisions() {
    auto first = diskRecord("disk", "8", "0");
    first.capacity_sectors_512 = "100";
    first.read_only = "1";
    auto second = first;
    second.capacity_sectors_512 = "broken";
    second.read_only.reset();
    const auto baseline = scanRecords({first, second});
    DL_CHECK(!baseline.devices.front().capacity_bytes);
    DL_CHECK(!baseline.devices.front().read_only);
    DL_CHECK(scanRecords({second, first}) == baseline);

    first.current_path = "/dev/shared";
    first.sysfs_path = "/sys/devices/shared";
    second = first;
    second.kernel_name = "another";
    auto snapshot = scanRecords({first, second});
    for (const auto& device : snapshot.devices) {
        DL_CHECK(!device.device_number);
        DL_CHECK(!device.current_path);
        DL_CHECK(!device.sysfs_path);
        DL_CHECK(hasIssue(device.issues, BlockInventoryIssueCode::DuplicateRecord,
                          "device_number"));
    }
    DL_CHECK(scanRecords({second, first}) == snapshot);

    second = first;
    second.node_type = "partition";
    second.parent_kernel_name = "parent";
    auto parent = diskRecord("parent", "8", "16");
    snapshot = scanRecords({first, second, parent});
    DL_CHECK(findDevice(snapshot, "disk")->node_type == BlockNodeType::Unknown);
    DL_CHECK(!findDevice(snapshot, "disk")->parent_kernel_name);
    DL_CHECK(findDevice(snapshot, "parent")->child_kernel_names.empty());
}

void testInvalidRelationshipsAndExplicitReadErrors() {
    auto disk = diskRecord("disk", "8", "0");
    auto child = partitionRecord("child", "8", "1", "child", "1");
    auto snapshot = scanRecords({disk, child});
    DL_CHECK(!findDevice(snapshot, "child")->parent_kernel_name);
    disk.partition_number = "1";
    disk.parent_kernel_name = "child";
    snapshot = scanRecords({disk, child});
    DL_CHECK(!findDevice(snapshot, "disk")->partition_number);
    DL_CHECK(!findDevice(snapshot, "disk")->parent_kernel_name);
    disk.model = "plausible fallback";
    disk.read_only = "0";
    disk.issues = {
        {BlockInventoryIssueCode::SourceReadFailure, "disk", "model", "denied", 13},
        {BlockInventoryIssueCode::ConflictingAttribute, "disk", "read_only", "disagrees"}
    };
    snapshot = scanRecords({disk});
    DL_CHECK(!snapshot.devices.front().model && !snapshot.devices.front().read_only);
    DL_CHECK(hasIssue(snapshot.devices.front().issues,
                      BlockInventoryIssueCode::SourceReadFailure, "model"));

    FakeLinuxInventorySource source;
    LinuxInventoryProvider provider(source);
    DL_CHECK(provider.scan().value().devices.empty());
    for (const auto code : {ErrorCode::Unavailable, ErrorCode::IoError, ErrorCode::PermissionDenied}) {
        source.fatal_error = Error{code, "fake", "initialization or scan failure"};
        DL_CHECK(provider.scan().error().code == code);
    }
    source.fatal_error.reset();
    source.snapshot.records = {disk};
    DL_CHECK(provider.scan().value().devices.size() == 1);
    source.snapshot.records.clear();
    DL_CHECK(provider.scan().value().devices.empty());
}

template <typename T>
concept HasIdentity = requires(T value) { value.identity; };
template <typename T>
concept HasSafety = requires(T value) { value.safety; };
template <typename T>
concept HasReadiness = requires(T value) { value.readiness; };
static_assert(!HasIdentity<BlockDeviceObservation>);
static_assert(!HasSafety<BlockDeviceObservation>);
static_assert(!HasReadiness<BlockDeviceObservation>);
static_assert(!std::is_convertible_v<BlockDeviceObservation, Drive>);
static_assert(!std::is_convertible_v<BlockDeviceObservation, DriveId>);

}  // namespace

int main() {
    return test::run([] {
        testPhysicalKinds();
        testSataFactMapping();
        testSasAndOpaquePaths();
        testNumericAndIdentifierEdges();
        testIncompleteDuplicatesAndLocatorCollisions();
        testInvalidRelationshipsAndExplicitReadErrors();
        testMissingAndBlankFacts();
        testMalformedAttributes();
        testCapacityBoundaries();
        testDiskPartitionRelationships();
        testNvmeUsbAndPseudoFacts();
        testDuplicateRecordsAndStableOrdering();
        testLocalIssuesAndFatalFailure();
    });
}
