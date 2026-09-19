#include "test_support.h"
#include "platform/linux/linux_host_usage.h"

#include <libudev.h>

#include <cerrno>
#include <map>

using namespace drivelab;

struct udev {};
struct udev_device {};
namespace {
struct State {
    std::map<std::string, std::string> properties;
    int contexts = 0;
    int references = 0;
    int opens = 0;
    int init_error = 0;
    int open_error = 0;
    int after_error = 0;
    int property_error = 0;
    bool changed = false;
} fake;

BlockDeviceObservation observation() {
    BlockDeviceObservation node;
    node.kernel_name = "sda1"; node.sysfs_path = "/sys/devices/test/sda1";
    node.device_number = BlockDeviceNumber{8, 1};
    return node;
}

void reset() {
    DL_CHECK(fake.contexts == 0 && fake.references == 0);
    fake = {};
    fake.properties = {{"MAJOR", "8"}, {"MINOR", "1"},
                       {"ID_FS_TYPE", "ext4"}, {"ID_FS_USAGE", "filesystem"}};
}

HostRead<SignatureSourceRecord> scan() {
    UdevSignatureSource source;
    auto result = source.read(observation());
    DL_CHECK(fake.contexts == 0 && fake.references == 0);
    return result;
}

void testNativeMetadata() {
    reset();
    auto result = scan();
    DL_CHECK(result.value && result.issues.empty());
    DL_CHECK(result.value->properties.size() == 2);
    fake.properties.erase("ID_FS_TYPE"); fake.properties.erase("ID_FS_USAGE");
    DL_CHECK(scan().value->properties.empty());
    reset(); fake.properties["ID_FS_AMBIVALENT"] = "1";
    std::vector<DiscoveryIssue> issues;
    DL_CHECK(parseSignature(scan(), "sda1", issues).state == SignatureState::Conflicting);
}

void testFailuresAndRaces() {
    reset(); fake.init_error = EACCES;
    auto result = scan(); DL_CHECK(!result.value && result.issues.front().native_error == EACCES);
    reset(); fake.open_error = ENODEV;
    DL_CHECK(scan().issues.front().code == DiscoveryIssueCode::Disappeared);
    reset(); fake.after_error = ENOENT; DL_CHECK(!scan().value);
    reset(); fake.property_error = EACCES;
    result = scan(); DL_CHECK(result.value && !result.issues.empty());
    std::vector<DiscoveryIssue> issues;
    DL_CHECK(parseSignature(result, "sda1", issues).state == SignatureState::Unavailable);
    reset(); fake.properties["MINOR"] = "2";
    DL_CHECK(scan().issues.front().code == DiscoveryIssueCode::SnapshotChanged);
    reset(); fake.properties["MAJOR"] = "bogus"; DL_CHECK(!scan().value);
    reset(); fake.changed = true; DL_CHECK(!scan().value);
    reset(); auto node = observation(); node.sysfs_path = "/dev/sda";
    UdevSignatureSource source; DL_CHECK(!source.read(node).value);
}
}  // namespace

extern "C" {
udev* udev_new() {
    if (fake.init_error) { errno = fake.init_error; return nullptr; }
    ++fake.contexts; return new udev;
}
udev* udev_unref(udev* context) { --fake.contexts; delete context; return nullptr; }
udev_device* udev_device_new_from_syspath(udev*, const char* path) {
    DL_CHECK(std::string(path) == "/sys/devices/test/sda1");
    ++fake.opens;
    const int error = fake.open_error ? fake.open_error : (fake.opens > 1 ? fake.after_error : 0);
    if (error) { errno = error; return nullptr; }
    ++fake.references; return new udev_device;
}
udev_device* udev_device_unref(udev_device* device) {
    --fake.references; delete device; return nullptr;
}
const char* udev_device_get_sysname(udev_device*) {
    return fake.changed && fake.opens > 1 ? "replaced" : "sda1";
}
const char* udev_device_get_property_value(udev_device*, const char* key) {
    if (fake.property_error && std::string(key).starts_with("ID_FS_")) {
        errno = fake.property_error; return nullptr;
    }
    const auto found = fake.properties.find(key);
    if (found == fake.properties.end()) { errno = ENOENT; return nullptr; }
    return found->second.c_str();
}
}

int main() {
    return test::run([] {
        testNativeMetadata();
        testFailuresAndRaces();
        reset();
    });
}
