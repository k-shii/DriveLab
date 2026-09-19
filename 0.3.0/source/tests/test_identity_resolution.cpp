#include "test_support.h"
#include "core/resolved_inventory.h"

#include <algorithm>
#include <random>
#include <type_traits>

using namespace drivelab;

namespace {

BlockDeviceObservation disk(std::string name, std::uint32_t minor, std::string wwn = "") {
    BlockDeviceObservation node;
    node.kernel_name = name;
    node.node_type = BlockNodeType::Disk;
    node.current_path = "/dev/" + name;
    node.sysfs_path = "/sys/devices/test/" + name;
    node.device_number = BlockDeviceNumber{8, minor};
    node.model = "Model-A";
    node.serial = "Serial-A";
    node.transport = "ata";
    node.capacity_bytes = 1000000000;
    if (!wwn.empty()) node.wwn = wwn;
    return node;
}

ResolvedBlockNode resolve(BlockDeviceObservation node) {
    return resolveBlockIdentities({{std::move(node)}, {}}).nodes.front();
}

void stableIdentityAndFreshPaths() {
    auto first = disk("sdc", 32, "0x50014EE001234567");
    auto first_snapshot = resolveBlockIdentities({{first}, {}});
    const auto id = *first_snapshot.nodes.front().identity.id;
    DL_CHECK(id.value == "prod:wwn:50014ee001234567");
    auto later = first;
    later.kernel_name = "sde"; later.current_path = "/dev/sde";
    later.device_number = BlockDeviceNumber{8, 64}; later.sysfs_path = "/sys/another-location";
    const auto later_snapshot = resolveBlockIdentities({{later}, {}});
    DL_CHECK(later_snapshot.nodes.front().identity.id == id);
    DL_CHECK(resolveCurrentPath(later_snapshot, id).value() == "/dev/sde");
    first.wwn = "50014ee007654321"; first.serial = "Different";
    const auto reused = resolveBlockIdentities({{first}, {}});
    DL_CHECK(reused.nodes.front().identity.id != id);
    DL_CHECK(!resolveCurrentPath(reused, id));
    first.wwn = "naa.50014ee001234567";
    DL_CHECK(resolve(first).identity.id == id);
}

void fallbackAndInsufficientEvidence() {
    auto node = disk("sda", 0);
    const auto id = resolve(node).identity.id;
    DL_CHECK(id);
    node.current_path = "/dev/new"; node.sysfs_path.reset();
    DL_CHECK(resolve(node).identity.id == id);
    node.serial = "serial-A";
    DL_CHECK(resolve(node).identity.id != id); // Serial/model bytes are case-sensitive.
    node.serial = "unknown"; DL_CHECK(!resolve(node).identity.id);
    node.serial.reset(); DL_CHECK(!resolve(node).identity.id);
    node.serial = "good"; node.model.reset(); DL_CHECK(!resolve(node).identity.id);
    node.model = "good"; node.capacity_bytes = 0; DL_CHECK(!resolve(node).identity.id);
    node.capacity_bytes = 1; node.transport = "usb"; DL_CHECK(!resolve(node).identity.id);
    node.transport.reset(); DL_CHECK(!resolve(node).identity.id);
    node.transport = "ata"; node.observation_kind = BlockObservationKind::VirtualOrPseudo;
    DL_CHECK(!resolve(node).identity.id);
    node.observation_kind = BlockObservationKind::Unknown;
    node.node_type = BlockNodeType::Partition; DL_CHECK(!resolve(node).identity.id);
    node = {}; node.kernel_name = "sda"; node.current_path = "/dev/sda";
    DL_CHECK(!resolve(node).identity.id);
}

void nvmeNamespaceRules() {
    auto node = disk("nvme0n1", 0);
    node.transport.reset();
    node.nvme_eui = "00 25 38 B9 12 34 56 78";
    node.nvme_nguid = "00112233-4455-6677-8899-AABBCCDDEEFF";
    auto result = resolve(node);
    DL_CHECK(result.identity.scope == IdentityScope::NvmeNamespace);
    DL_CHECK(result.identity.id->value == "prod:nvme-nguid:00112233445566778899aabbccddeeff");
    DL_CHECK(result.identity.strong_aliases.size() == 2);
    node.wwn = "eui.00112233445566778899aabbccddeeff";
    DL_CHECK(resolve(node).identity.id == result.identity.id);
    node.wwn = "eui.002538b912345678";
    DL_CHECK(resolve(node).identity.id == result.identity.id);
    node.wwn = "eui.002538b912345679"; DL_CHECK(!resolve(node).identity.id);
    node.wwn.reset(); node.nvme_nguid.reset();
    DL_CHECK(resolve(node).identity.id->value == "prod:nvme-eui:002538b912345678");
    node.nvme_eui = "0000000000000000"; DL_CHECK(!resolve(node).identity.id);
    node.nvme_eui.reset(); node.transport = "nvme";
    DL_CHECK(!resolve(node).identity.id); // Controller serial is not namespace identity.
    node.wwn = "uuid.00112233-4455-6677-8899-aabbccddeeff";
    DL_CHECK(!resolve(node).identity.id);
}

void conflictsAndCollisions() {
    auto a = disk("sda", 0, "50014ee001234567");
    a.issues.push_back({BlockInventoryIssueCode::ConflictingAttribute, "sda", "wwn", "two values"});
    DL_CHECK(!resolve(a).identity.id);
    a.issues.clear(); a.wwn = "garbage"; DL_CHECK(!resolve(a).identity.id);
    a.wwn = "0000000000000000"; DL_CHECK(!resolve(a).identity.id);
    a.wwn = "ffffffffffffffff"; DL_CHECK(!resolve(a).identity.id);
    a.wwn = "50014ee001234567";
    auto b = disk("sdb", 16, "50014ee001234567");
    auto collision = resolveBlockIdentities({{a, b}, {}});
    for (const auto& node : collision.nodes) {
        DL_CHECK(node.identity.state == ResolutionState::Ambiguous);
        DL_CHECK(!node.identity.id && !node.current_path);
    }
    // Even an unresolved observation's valid strong alias poisons a collision.
    b.issues.push_back({BlockInventoryIssueCode::SourceReadFailure, "sdb", "model", "denied"});
    collision = resolveBlockIdentities({{a, b}, {}});
    DL_CHECK(collision.nodes.front().identity.state == ResolutionState::Ambiguous);
    b.wwn.reset(); b.issues.clear();
    collision = resolveBlockIdentities({{a, b}, {}});
    DL_CHECK(!collision.nodes.front().identity.id); // Strong versus fallback collision.
    a.wwn.reset();
    collision = resolveBlockIdentities({{a, b}, {}});
    DL_CHECK(!collision.nodes.front().identity.id); // Duplicate fallback.
    a.nvme_nguid = "00112233445566778899aabbccddeeff";
    b.nvme_nguid = "11112233445566778899aabbccddeeff";
    a.nvme_eui = b.nvme_eui = "002538b912345678";
    collision = resolveBlockIdentities({{a, b}, {}});
    DL_CHECK(!collision.nodes[0].identity.id && !collision.nodes[1].identity.id);
    // No arbitrary NGUID choice when a lower-precedence EUI collides.
    b.nvme_eui = "002538b912345679";
    DL_CHECK(resolveBlockIdentities({{a, b}, {}}).nodes[0].identity.id);
}

void deterministicOrderAndLocatorCollision() {
    auto a = disk("sda", 0, "50014ee001234567");
    auto b = disk("sdb", 16, "50014ee007654321");
    b.serial = "Serial-B";
    auto c = disk("sdc", 32);
    c.serial = "Serial-C";
    const BlockInventorySnapshot raw{{a, b, c}, {}};
    const auto baseline = resolveBlockIdentities(raw);
    for (unsigned int seed = 0; seed < 32; ++seed) {
        auto shuffled = raw;
        std::mt19937 random(seed);
        std::shuffle(shuffled.devices.begin(), shuffled.devices.end(), random);
        DL_CHECK(resolveBlockIdentities(shuffled) == baseline);
    }
    b.current_path = a.current_path;
    const auto bad = resolveBlockIdentities({{a, b}, {}});
    DL_CHECK(!bad.nodes[0].identity.id && !bad.nodes[1].identity.id);
}

}  // namespace

int main() {
    return test::run([] {
        stableIdentityAndFreshPaths();
        fallbackAndInsufficientEvidence();
        nvmeNamespaceRules();
        conflictsAndCollisions();
        deterministicOrderAndLocatorCollision();
    });
}
