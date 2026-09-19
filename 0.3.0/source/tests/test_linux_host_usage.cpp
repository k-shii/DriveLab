#include "test_support.h"
#include "platform/linux/linux_host_usage.h"

#include <algorithm>
#include <cerrno>
#include <map>
#include <random>

using namespace drivelab;

namespace {
constexpr const char* swap_header = "Filename\tType\tSize\tUsed\tPriority\n";

class Inventory final : public BlockInventoryProvider {
public:
    BlockInventorySnapshot snapshot;
    int scans = 0;
    bool fail = false;
    Result<BlockInventorySnapshot> scan() override {
        ++scans;
        if (fail) return Result<BlockInventorySnapshot>::failure({ErrorCode::IoError, "fake", "failed"});
        return Result<BlockInventorySnapshot>::success(snapshot);
    }
};

class Files final : public LinuxHostFiles {
public:
    HostRead<std::string> mounts{std::string{}, {}};
    HostRead<std::string> swaps{std::string(swap_header), {}};
    std::map<std::string, HostRead<HostPathStatus>> paths;
    std::map<std::string, int> reads;
    std::string disappear;
    HostRead<std::string> read(HostTextFile file) override {
        return file == HostTextFile::MountInfo ? mounts : swaps;
    }
    HostRead<HostPathStatus> inspect(const std::string& path) override {
        if (++reads[path] > 1 && path == disappear) {
            return {std::nullopt, {{DiscoveryIssueCode::Disappeared, "", path, "gone", ENOENT}}};
        }
        const auto found = paths.find(path);
        if (found != paths.end()) return found->second;
        return {std::nullopt, {{DiscoveryIssueCode::ReadFailure, "", path, "denied", EACCES}}};
    }
};

class Signatures final : public LinuxSignatureSource {
public:
    std::map<std::string, HostRead<SignatureSourceRecord>> records;
    HostRead<SignatureSourceRecord> read(const BlockDeviceObservation& node) override {
        return records[node.kernel_name];
    }
};

BlockDeviceObservation disk(std::string name, std::uint32_t minor) {
    BlockDeviceObservation node;
    node.kernel_name = name; node.current_path = "/dev/" + name;
    node.node_type = BlockNodeType::Disk; node.device_number = BlockDeviceNumber{8, minor};
    node.wwn = "50014ee001234567";
    return node;
}

struct Fixture {
    Inventory inventory;
    Files files;
    Signatures signatures;
    Fixture() {
        auto d = disk("sda", 0);
        d.child_kernel_names = {"sda1", "sda2"};
        auto first = disk("sda1", 1);
        first.node_type = BlockNodeType::Partition; first.wwn.reset();
        first.partition_number = 1; first.parent_kernel_name = "sda";
        auto second = first; second.kernel_name = "sda2"; second.current_path = "/dev/sda2";
        second.device_number = BlockDeviceNumber{8, 2}; second.partition_number = 2;
        inventory.snapshot.devices = {d, first, second};
        for (const auto& n : inventory.snapshot.devices) {
            files.paths[*n.current_path] = {HostPathStatus{true, n.device_number}, {}};
        }
        signatures.records["sda1"] = {SignatureSourceRecord{{
            {"ID_FS_TYPE", "ext4"}, {"ID_FS_USAGE", "filesystem"}, {"ID_FS_UUID", "fixture-uuid"}}}, {}};
        signatures.records["sda2"] = {SignatureSourceRecord{{
            {"ID_FS_TYPE", "swap"}, {"ID_FS_USAGE", "other"}}}, {}};
    }
    ResolvedInventorySnapshot scan() {
        LinuxResolvedInventoryProvider provider(inventory, files, signatures);
        auto result = provider.scan(); DL_CHECK(result); return result.value();
    }
};

ResolvedBlockNode node(const ResolvedInventorySnapshot& snapshot, const std::string& name) {
    const auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
        [&](const auto& n) { return n.observation.kernel_name == name; });
    DL_CHECK(found != snapshot.nodes.end()); return *found;
}

bool issue(const std::vector<DiscoveryIssue>& issues, DiscoveryIssueCode code) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& i) { return i.code == code; });
}

void directUsageAndPropagation() {
    Fixture f;
    f.files.mounts.value =
        "30 1 8:1 / / rw shared:1 - ext4 /dev/sda1 rw\n"
        "31 30 8:1 /boot /boot rw - ext4 /dev/sda1 rw\n"
        "32 30 8:1 /efi /boot/efi rw - ext4 /dev/sda1 rw\n"
        "33 30 8:1 /home /home rw - ext4 /dev/sda1 rw\n"
        "34 30 8:1 /data /media/my\\040disk rw - ext4 /dev/sda1 rw\n";
    f.files.swaps.value = std::string(swap_header) + "/dev/sda2 partition 1024 42 -2\n";
    const auto out = f.scan();
    const auto parent = node(out, "sda"), child = node(out, "sda1");
    DL_CHECK(out.mounts.size() == 5 && out.swaps.size() == 1);
    DL_CHECK(parent.mounts.size() == 5 && child.mounts.size() == 5);
    DL_CHECK(parent.swaps.size() == 1 && node(out, "sda2").swaps.size() == 1);
    DL_CHECK(parent.usage_nodes == std::vector<std::string>({"sda", "sda1", "sda2"}));
    DL_CHECK(out.mounts[0].mount_point == "/" && out.mounts[1].mount_point == "/boot");
    DL_CHECK(out.mounts[2].mount_point == "/boot/efi" && out.mounts[3].mount_point == "/home");
    DL_CHECK(out.mounts[4].mount_point == "/media/my disk");
    DL_CHECK(out.mounts[1].subtree && out.mounts[0].node == "sda1");
    DL_CHECK(child.signature.state == SignatureState::Reported);
    DL_CHECK(child.signature.provenance.find("freshness unverified") != std::string::npos);
    DL_CHECK(out.mountinfo_read_complete && out.swaps_read_complete);
    DL_CHECK(issue(out.issues, DiscoveryIssueCode::UnsupportedTopology)); // Bind/subvolume distinction.
}

void sourcePathsDoNotOverrideDeviceNumbers() {
    Fixture f;
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 /dev/reused rw\n";
    f.files.paths["/dev/reused"] = {HostPathStatus{true, BlockDeviceNumber{8, 2}}, {}};
    auto out = f.scan();
    DL_CHECK(out.mounts.front().node == "sda1");
    DL_CHECK(issue(out.issues, DiscoveryIssueCode::SnapshotChanged));
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 UUID=opaque rw\n";
    DL_CHECK(f.scan().mounts.front().node == "sda1");
}

void deferredTopologyAndUnknownSources() {
    Fixture f;
    auto dm = disk("dm-0", 10); dm.device_number = BlockDeviceNumber{253, 0};
    dm.wwn.reset(); dm.observation_kind = BlockObservationKind::VirtualOrPseudo;
    f.inventory.snapshot.devices.push_back(dm);
    f.files.paths["/dev/mapper/vg-root"] = {HostPathStatus{true, dm.device_number}, {}};
    f.files.mounts.value =
        "1 1 253:0 / / rw - ext4 /dev/mapper/vg-root rw\n"
        "2 1 0:42 / /proc rw - proc proc rw\n";
    f.files.swaps.value = std::string(swap_header) +
        "/dev/mapper/vg-root partition 1024 0 -2\n/swapfile file 100 0 -3\n";
    const auto out = f.scan();
    DL_CHECK(out.mounts[0].node == "dm-0" && !out.mounts[1].node);
    DL_CHECK(!node(out, "dm-0").identity.id && node(out, "sda").mounts.empty());
    DL_CHECK(out.swaps[0].node == "dm-0" && !out.swaps[1].node);
    DL_CHECK(issue(out.issues, DiscoveryIssueCode::UncorrelatedUsage));
    DL_CHECK(issue(node(out, "dm-0").issues, DiscoveryIssueCode::UnsupportedTopology));
}

void malformedAndReadFailures() {
    Fixture f;
    f.files.mounts.value =
        "bad\n1 1 8:1 / /bad\\999 rw - ext4 /dev/sda1 rw\n"
        "3 1 8:1 / /valid rw - ext4 /dev/sda1 rw\n";
    f.files.swaps.value = std::string(swap_header) +
        "/dev/sda2 partition -4 0 -2\n/dev/sda2 partition 10 11 -2\n";
    auto out = f.scan();
    DL_CHECK(out.mounts.size() == 1 && out.swaps.empty());
    DL_CHECK(!out.mountinfo_read_complete && !out.swaps_read_complete);
    DL_CHECK(issue(out.issues, DiscoveryIssueCode::MalformedEvidence));
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 /dev/sda1 rw";
    f.files.swaps.value = std::string(swap_header) + "/dev/sda2 partition 10 0 -2";
    out = f.scan();
    DL_CHECK(out.mounts.empty() && out.swaps.empty());
    DL_CHECK(!out.mountinfo_read_complete && !out.swaps_read_complete);
    f.files.mounts = {std::nullopt, {{DiscoveryIssueCode::ReadFailure, "", "mountinfo", "denied", EACCES}}};
    f.files.swaps = {std::nullopt, {{DiscoveryIssueCode::ReadFailure, "", "swaps", "failed", EIO}}};
    out = f.scan();
    DL_CHECK(!out.mountinfo_read_complete && !out.swaps_read_complete);
    DL_CHECK(node(out, "sda").mounts.empty()); // No safety/readiness field is produced.
    DL_CHECK(std::any_of(out.issues.begin(), out.issues.end(),
        [](const auto& i) { return i.native_error == EACCES; }));
    f.inventory.fail = true;
    LinuxResolvedInventoryProvider provider(f.inventory, f.files, f.signatures);
    DL_CHECK(!provider.scan());
}

void signaturesAndDisappearance() {
    Fixture f;
    auto out = f.scan();
    DL_CHECK(node(out, "sda").signature.state == SignatureState::Unavailable);
    f.signatures.records["sda1"].value->properties.push_back({"ID_FS_TYPE", "xfs"});
    out = f.scan();
    DL_CHECK(node(out, "sda1").signature.state == SignatureState::Conflicting);
    DL_CHECK(!node(out, "sda1").signature.type);
    f.signatures.records["sda1"] = {SignatureSourceRecord{{
        {"ID_FS_TYPE", "bad\ntype"}, {"ID_FS_USAGE", "filesystem"}}}, {}};
    DL_CHECK(node(f.scan(), "sda1").signature.state == SignatureState::Malformed);
    f.signatures.records["sda1"] = {SignatureSourceRecord{{
        {"ID_FS_TYPE", "ext4"}, {"ID_FS_USAGE", "alien"}}}, {}};
    DL_CHECK(node(f.scan(), "sda1").signature.state == SignatureState::Unsupported);
    f.signatures.records["sda1"] = {std::nullopt, {{
        DiscoveryIssueCode::ReadFailure, "sda1", "signature", "denied", EACCES}}};
    DL_CHECK(node(f.scan(), "sda1").signature.state == SignatureState::Unavailable);
    f.files.reads.clear(); f.files.disappear = "/dev/sda";
    out = f.scan();
    DL_CHECK(!node(out, "sda").current_path);
    DL_CHECK(issue(node(out, "sda").issues, DiscoveryIssueCode::SnapshotChanged));
    const auto id = *node(out, "sda").identity.id;
    DL_CHECK(!resolveCurrentPath(out, id));
}

void repeatedScansAndOrder() {
    Fixture f;
    f.files.mounts.value = "2 1 8:1 / /home rw - ext4 /dev/sda1 rw\n1 1 8:1 / / rw - ext4 /dev/sda1 rw\n";
    const auto baseline = f.scan();
    for (unsigned seed = 0; seed < 16; ++seed) {
        std::mt19937 random(seed);
        std::shuffle(f.inventory.snapshot.devices.begin(), f.inventory.snapshot.devices.end(), random);
        DL_CHECK(f.scan() == baseline);
    }
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 /dev/sda1 rw\n2 1 8:1 / /home rw - ext4 /dev/sda1 rw\n";
    DL_CHECK(f.scan() == baseline);
    LinuxResolvedInventoryProvider provider(f.inventory, f.files, f.signatures);
    const auto id = *node(baseline, "sda").identity.id;
    for (auto& n : f.inventory.snapshot.devices) {
        if (n.kernel_name == "sda") n.current_path = "/dev/sde";
    }
    f.files.paths["/dev/sde"] = {HostPathStatus{true, BlockDeviceNumber{8, 0}}, {}};
    DL_CHECK(provider.resolvePath(id).value() == "/dev/sde");
    for (auto& n : f.inventory.snapshot.devices) {
        if (n.kernel_name == "sda") n.wwn = "50014ee007654321";
    }
    DL_CHECK(!provider.resolvePath(id));
}

void structuredUncorrelatedMount() {
    Fixture f;
    f.files.mounts.value = "91 1 0:22 / /proc rw - proc proc rw\n";
    const auto out = f.scan();
    DL_CHECK(out.mountinfo_read_complete);
    DL_CHECK(out.mounts.size() == 1 && !out.mounts[0].node);
    const auto found = std::find_if(out.issues.begin(), out.issues.end(), [](const auto& i) {
        return i.code == DiscoveryIssueCode::UncorrelatedUsage && i.field == "mountinfo";
    });
    DL_CHECK(found != out.issues.end() && found->mount_id == 91);
}
void duplicateRecordsAndBadRelationships() {
    Fixture f;
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 /dev/sda1 rw\n1 1 8:2 / /other rw - swap /dev/sda2 rw\n";
    f.files.swaps.value = std::string(swap_header) +
        "/dev/sda2 partition 100 0 -2\n/dev/sda2 partition 100 0 -2\n";
    auto out = f.scan();
    DL_CHECK(!out.mounts[0].node && !out.mounts[1].node);
    DL_CHECK(!out.swaps[0].node && !out.swaps[1].node);
    f.files.mounts.value = "1 1 8:1 / / rw - ext4 /dev/sda1 rw\n";
    f.inventory.snapshot.devices[0].child_kernel_names.clear();
    DL_CHECK(node(f.scan(), "sda").mounts.empty());
}
}  // namespace

int main() {
    return test::run([] {
        directUsageAndPropagation();
        sourcePathsDoNotOverrideDeviceNumbers();
        deferredTopologyAndUnknownSources();
        malformedAndReadFailures();
        signaturesAndDisappearance();
        repeatedScansAndOrder();
        duplicateRecordsAndBadRelationships();
        structuredUncorrelatedMount();
    });
}
