#pragma once
#include "test_support.h"
#include "platform/linux/linux_ownership_source.h"
#include <algorithm>
#include <map>

namespace ownership_test {
using namespace drivelab;
class Io final : public OwnershipIo {
public:
    std::map<std::string, OwnershipRead<std::string>> texts, links;
    std::map<std::string, OwnershipRead<std::vector<std::string>>> lists;
    std::map<std::string, OwnershipRead<OwnershipPath>> paths;
    std::map<std::string, int> reads;
    std::string change;
    std::string disappear_location;
    OwnershipRead<std::string> text(const std::string& path) override {
        if (++reads[path] > 1 && path == change) return {std::string("changed"), false};
        return texts.contains(path) ? texts.at(path) : OwnershipRead<std::string>{std::nullopt, true};
    }
    OwnershipRead<std::vector<std::string>> list(const std::string& path) override {
        return lists.contains(path) ? lists.at(path) : OwnershipRead<std::vector<std::string>>{std::nullopt, true};
    }
    OwnershipRead<std::string> canonical(const std::string& path) override {
        return links.contains(path) ? links.at(path) : OwnershipRead<std::string>{std::nullopt, true};
    }
    OwnershipRead<OwnershipPath> locate(const std::string& path) override {
        if (++reads["stat:" + path] > 1 && path == disappear_location) return {std::nullopt,true};
        return paths.contains(path) ? paths.at(path) : OwnershipRead<OwnershipPath>{std::nullopt, true};
    }
};
class Zfs final : public ZfsOwnershipSource {
public:
    ZfsOwnershipRead result{true, {}, ""};
    int reads = 0;
    std::vector<ZfsOwnershipRead> sequence;
    ZfsOwnershipRead read() override {
        const auto index = static_cast<std::size_t>(reads++);
        return index < sequence.size() ? sequence[index] : result;
    }
};
struct Fixture {
    ResolvedInventorySnapshot inventory;
    Io io;
    Zfs zfs;
    Fixture() {
        inventory.mountinfo_read_complete = inventory.swaps_read_complete = true;
        io.lists["/proc"] = {std::vector<std::string>{"1"}, false};
        io.lists["/proc/1/task"] = {std::vector<std::string>{"1"}, false};
        io.paths["/proc/1/cwd"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        io.paths["/proc/1/root"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        io.texts["/proc/1/maps"] = {std::string{}, false};
        io.lists["/proc/1/fd"] = {std::vector<std::string>{}, false};
        io.links["/proc/self/ns/mnt"] = {std::string("mnt:[1]"), false};
        io.links["/proc/1/ns/mnt"] = {std::string("mnt:[1]"), false};
        io.paths["/proc"] = {OwnershipPath{false,{0,3}, true, false, std::nullopt, false},false};
        io.texts["/proc/self/mountinfo"] = {std::string("1 0 0:1 / / rw - tmpfs tmpfs rw\n2 1 0:3 / /proc rw - proc proc rw\n"), false};
        io.texts["/proc/swaps"] = {std::string("Filename Type Size Used Priority\n"), false};
        add("sda", {8, 0});
    }
    void add(const std::string& name, BlockDeviceNumber dev, bool virtual_node = false) {
        ResolvedBlockNode b;
        b.observation.kernel_name = name;
        b.observation.node_type = BlockNodeType::Disk;
        b.observation.device_number = dev;
        b.observation.current_path = "/dev/" + name;
        b.observation.sysfs_path = "/sys/devices/pci0000:00/0000:00:01.0/block/" + name;
        b.observation.read_only = false;
        b.observation.capacity_bytes = 1024 * 1024;
        b.current_path = "/dev/" + name;
        b.signature.state = SignatureState::Reported;
        b.signature.type = "ext4"; b.signature.usage = "filesystem";
        if (!virtual_node) { b.identity.state = ResolutionState::Resolved; b.identity.id = DriveId{"fixture:" + name}; }
        else {
            b.observation.observation_kind = BlockObservationKind::VirtualOrPseudo;
            b.issues.push_back({DiscoveryIssueCode::UnsupportedTopology, name, "identity", "B intentionally defers virtual identity", {}});
        }
        inventory.nodes.push_back(b);
        const auto base = "/sys/class/block/" + name;
        io.texts[base + "/dev"] = {std::to_string(dev.major_number) + ":" + std::to_string(dev.minor_number) + "\n", false};
        io.lists[base + "/holders"] = {std::vector<std::string>{}, false};
        io.lists[base + "/slaves"] = {std::vector<std::string>{}, false};
        std::vector<std::string> names;
        for (const auto& n : inventory.nodes) names.push_back(n.observation.kernel_name);
        io.lists["/sys/class/block"] = {names, false};
        io.paths["/dev/" + name] = {OwnershipPath{true, dev, true, false, std::nullopt, false}, false};
    }
    ResolvedBlockNode& node(const std::string& name) {
        return *std::find_if(inventory.nodes.begin(), inventory.nodes.end(),
            [&](const auto& n) { return n.observation.kernel_name == name; });
    }
    void partition(const std::string& name, const std::string& parent, BlockDeviceNumber dev) {
        add(name, dev);
        auto& n = node(name);
        n.observation.node_type = BlockNodeType::Partition;
        n.observation.partition_number = 1;
        n.observation.parent_kernel_name = parent;
        n.identity = {};
        // Real Linux partitions have holders, but no slaves directory.
        io.lists.erase("/sys/class/block/" + name + "/slaves");
        node(parent).observation.child_kernel_names.push_back(name);
    }
    void link(const std::string& consumer, const std::string& provider) {
        io.lists["/sys/class/block/" + consumer + "/slaves"].value->push_back(provider);
        io.lists["/sys/class/block/" + provider + "/holders"].value->push_back(consumer);
    }
    void dm(const std::string& name, std::uint32_t minor, const std::string& backing, bool lvm = true) {
        add(name, {253, minor}, true);
        link(name, backing);
        const auto base = "/sys/class/block/" + name + "/dm";
        io.lists[base] = {std::vector<std::string>{"uuid", "name"}, false};
        io.texts[base + "/uuid"] = {lvm ? "LVM-" + std::string(32, 'v') + std::string(31, 'l') + std::to_string(minor) : "CRYPT-fixture", false};
        io.texts[base + "/name"] = {lvm ? "pve-volume" + std::to_string(minor) : "crypt", false};
    }
    void mount(const std::string& name, const std::string& point) {
        MountEvidence m;
        m.mount_id = inventory.mounts.size() + 1;
        m.node = name; m.mount_point = point; m.root = "/"; m.filesystem_type = "ext4";
        m.source = "/dev/" + name; m.device_number = *node(name).observation.device_number;
        inventory.mounts.push_back(m);
    }
    void swap(const std::string& name) {
        SwapEvidence s; s.path = "/dev/" + name; s.type = "partition"; s.node = name;
        s.device_number = node(name).observation.device_number; inventory.swaps.push_back(s);
        *io.texts["/proc/swaps"].value += "/dev/" + name + " partition 1024 0 -2\n";
    }
    OwnershipEvidence evidence() { LinuxOwnershipSource source(io, zfs); return source.read(inventory); }
    OwnershipAssessment assess() { return assessOwnership(inventory, evidence()); }
    void pve() {
        io.lists["/etc/pve"] = {std::vector<std::string>{"local", "storage.cfg"}, false};
        io.links["/etc/pve/local"] = {std::string("/etc/pve/nodes/pve"), false};
        io.lists["/etc/pve/nodes/pve/qemu-server"] = {std::vector<std::string>{}, false};
        io.lists["/etc/pve/nodes/pve/lxc"] = {std::vector<std::string>{}, false};
        io.texts["/etc/pve/storage.cfg"] = {std::string{}, false};
    }
    void guest(const std::string& text) {
        io.lists["/etc/pve/nodes/pve/qemu-server"].value->push_back("100.conf");
        io.texts["/etc/pve/nodes/pve/qemu-server/100.conf"] = {text, false};
    }
};
inline ClassifiedStorage device(const OwnershipAssessment& a, const std::string& name = "sda") {
    const auto it = std::find_if(a.physical_devices.begin(), a.physical_devices.end(),
        [&](const auto& d) { return d.node == blockOwnershipKey(name); });
    DL_CHECK(it != a.physical_devices.end()); return *it;
}
inline OwnershipTrace trace(const OwnershipAssessment& a, const std::string& key) {
    const auto it = std::find_if(a.traces.begin(), a.traces.end(),
        [&](const auto& t) { return t.consumer == key; });
    DL_CHECK(it != a.traces.end()); return *it;
}
inline bool reason(const ClassifiedStorage& d, OwnershipReasonCode code) {
    return std::any_of(d.reasons.begin(), d.reasons.end(), [&](const auto& r) { return r.code == code; });
}
}
