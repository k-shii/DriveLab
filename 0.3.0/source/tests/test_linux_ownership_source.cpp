#include "ownership_fixture.h"
using namespace ownership_test;
namespace {
void proxmoxDmShape() {
    Fixture f;
    f.partition("sda3", "sda", {8, 3});
    f.node("sda3").signature.type = "LVM2_member";
    f.dm("dm-0", 0, "sda3");
    f.dm("dm-1", 1, "sda3");
    f.swap("dm-0"); f.mount("dm-1", "/");
    const auto a = f.assess();
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ActiveSwap));
    DL_CHECK(reason(device(a), OwnershipReasonCode::SystemRoot));
    DL_CHECK(reason(device(a), OwnershipReasonCode::LvmMember));
    for (const auto* name : {"dm-0", "dm-1"}) {
        const auto t = trace(a, blockOwnershipKey(name));
        DL_CHECK(t.complete && t.owners.size() == 1 && t.owners.front().node == "block:sda");
        DL_CHECK(f.node(name).identity.state == ResolutionState::Unresolved);
        DL_CHECK(f.node(name).issues.front().code == DiscoveryIssueCode::UnsupportedTopology);
    }
    DL_CHECK(std::count_if(a.nodes.begin(), a.nodes.end(), [](const auto& n) { return n.kind == OwnershipKind::LvmVg; }) == 1);
    DL_CHECK(std::any_of(a.nodes.begin(), a.nodes.end(), [](const auto& n) { return n.kind == OwnershipKind::LvmPv; }));
    // Ordinary mounted filesystem, independent from root and LVM metadata.
    Fixture other; other.dm("dm-0", 0, "sda", false); other.mount("dm-0", "/data");
    const auto mounted = other.assess();
    DL_CHECK(device(mounted).status == DriveStatus::Protected);
    DL_CHECK(reason(device(mounted), OwnershipReasonCode::Mount));
}
void brokenChains() {
    for (int mode = 0; mode < 7; ++mode) {
        Fixture f; f.dm("dm-0", 0, "sda"); f.swap("dm-0");
        const auto base = "/sys/class/block/";
        if (mode == 0) f.io.lists[std::string(base) + "sda/holders"].value->clear();
        if (mode == 1) f.io.lists[std::string(base) + "dm-0/slaves"].value->clear();
        if (mode == 2) f.io.lists[std::string(base) + "sda/holders"] = {};
        if (mode == 3) f.io.lists[std::string(base) + "dm-0/slaves"].value->push_back("missing");
        if (mode == 4) f.io.lists[std::string(base) + "sda/holders"].value->push_back("dm-0");
        if (mode == 5) f.io.change = std::string(base) + "dm-0/dev";
        if (mode == 6) f.io.texts[std::string(base) + "dm-0/dm/uuid"].value = "LVM-invalid";
        const auto a = f.assess();
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(!a.issues.empty());
    }
    Fixture f; f.dm("dm-0", 0, "sda"); f.dm("dm-1", 1, "dm-0");
    f.link("dm-0", "dm-1"); f.mount("dm-1", "/");
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);
}
void mdAndZfs() {
    Fixture f; f.add("sdb", {8,16}); f.add("md0", {9,0}, true);
    f.link("md0", "sda"); f.link("md0", "sdb");
    const std::string base = "/sys/class/block/md0/md";
    f.io.lists[base] = {std::vector<std::string>{}, false};
    for (const auto& [key, value] : std::map<std::string,std::string>{
        {"array_state","clean"}, {"level","raid1"}, {"raid_disks","2"}, {"degraded","0"}})
        f.io.texts[base + "/" + key] = {value, false};
    f.mount("md0", "/data");
    const auto a = f.assess();
    for (const auto* disk : {"sda","sdb"}) {
        DL_CHECK(device(a, disk).status == DriveStatus::Protected);
        DL_CHECK(reason(device(a, disk), OwnershipReasonCode::RaidMember));
    }
    f.io.texts[base + "/degraded"].value = "1";
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);

    Fixture z; z.add("sdb", {8,16});
    z.io.lists["/sys/module/zfs"] = {std::vector<std::string>{}, false};
    z.zfs.result.pools.push_back({"rpool","42", {"root","root",{}, {
        {"mirror","mirror",{}, {{"a","disk","/dev/sda",{},true}, {"b","disk","/dev/sdb",{},true}},true}},true},true});
    MountEvidence m; m.mount_id=1; m.filesystem_type="zfs"; m.source="rpool/ROOT/pve"; m.mount_point="/";
    z.inventory.mounts.push_back(m);
    const auto za = z.assess();
    DL_CHECK(device(za).status == DriveStatus::Protected);
    DL_CHECK(reason(device(za), OwnershipReasonCode::SystemRoot));
    DL_CHECK(trace(za, "zfs-pool:42").owners.size() == 2);
    z.zfs.result.pools.front().root.children.front().children.back().path = "/dev/missing";
    DL_CHECK(device(z.assess()).status == DriveStatus::Unknown);
    z.zfs.result = {false, {}, "unavailable"};
    DL_CHECK(device(z.assess()).status == DriveStatus::Unknown);
}
void nestedLvmAndZvol() {
    Fixture f;
    f.dm("dm-0", 0, "sda", false); // LUKS backing an active LVM PV
    f.dm("dm-1", 1, "dm-0");
    f.mount("dm-1", "/");
    auto a = f.assess();
    DL_CHECK(trace(a, "block:dm-1").complete);
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(std::any_of(a.edges.begin(), a.edges.end(), [](const auto& e) {
        return e.consumer.starts_with("lvm-pv:") && e.provider == "block:dm-0";
    }));
    Fixture z;
    z.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
    z.zfs.result.pools.push_back({"rpool","42",{"root","root",{},{{"disk","disk","/dev/sda",{},true}},true},true});
    z.add("zd0", {230,0}, true);
    z.io.lists["/dev/zvol/rpool"] = {std::vector<std::string>{"swap"},false};
    z.io.paths["/dev/zvol/rpool/swap"] = {OwnershipPath{true,{230,0}, true, false, std::nullopt, false},false};
    z.swap("zd0");
    a = z.assess();
    DL_CHECK(trace(a,"block:zd0").complete);
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ActiveSwap));
}
OwnershipAssessment withFreshEvidence(Fixture& f) {
    auto e = f.evidence();
    for (const auto& b : f.inventory.nodes) {
        FreshSignatureEvidence fresh;
        fresh.state = FreshSignatureState::Filesystem; fresh.type = "ext4"; fresh.usage = "filesystem";
        e.fresh_signatures[blockOwnershipKey(b.observation.kernel_name)] = fresh;
    }
    e.coverage["inactive_membership"] = true;
    std::erase_if(e.issues,[](const auto& i) { return i.source == "inactive_membership"; });
    return assessOwnership(f.inventory,std::move(e));
}
void runtimeGuestReferences() {
    Fixture f; f.add("guest-storage",{8,16}); f.pve();
    f.guest("scsi0: /dev/guest-storage\n");
    // Configured storage is positive evidence. Actual ordinary runtime handles
    // are independently attributed by metadata; no process name/PID exception.
    f.io.lists["/proc/1/fd"].value->push_back("3");
    f.io.paths["/proc/1/fd/3"] = {OwnershipPath{true,{8,16}, true, false, std::nullopt, false},false};
    auto a = withFreshEvidence(f);
    DL_CHECK(device(a).status == DriveStatus::Ready);
    DL_CHECK(device(a,"guest-storage").status == DriveStatus::Protected);
    DL_CHECK(reason(device(a,"guest-storage"),OwnershipReasonCode::GuestAssignment));
    DL_CHECK(reason(device(a,"guest-storage"),OwnershipReasonCode::OpenBlockHandle));
    // The runtime owner wins even if configuration never mentioned this disk.
    f.io.paths["/proc/1/fd/3"].value->device = {8,0};
    a = withFreshEvidence(f);
    DL_CHECK(device(a).status == DriveStatus::Busy);
    DL_CHECK(reason(device(a),OwnershipReasonCode::OpenBlockHandle));
    f.io.paths["/proc/1/fd/3"] = {};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.paths["/proc/1/fd/3"] = {OwnershipPath{true,{8,16}, true, false, std::nullopt, false},false};
    f.io.lists["/proc/1/fd"].value->push_back("4");
    f.io.paths["/proc/1/fd/4"] = {OwnershipPath{false,{0,88},false,false, std::nullopt, false},false};
    // These supplied side-channel diagnostics are deliberately NOT negative
    // authority: neither matching cmdline nor zero UserFiles proves that
    // accepted in-flight io_uring requests retain no other files.
    f.io.texts["/proc/1/cmdline"] = {std::string("qemu-system-x86_64 -drive /dev/guest-storage"),false};
    f.io.texts["/proc/1/fdinfo/4"] = {std::string("UserFiles:\t0\n"),false};
    for (const auto* object : {"anon_inode:[io_uring]","anon_inode:kvm-vm",
         "anon_inode:kvm-vcpu:0","anon_inode:kvm-vcpu-stats:0","anon_inode:kvm-vfio",
         "anon_inode:[vfio-device]","/dmabuf:","opaque-kernel-object"}) {
        f.io.links["/proc/1/fd/4"] = {std::string(object),false};
        DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    }
    f.io.links["/proc/1/fd/4"].value = "anon_inode:[io_uring]";
    const auto matching = withFreshEvidence(f);
    f.io.texts["/proc/1/cmdline"].value = "qemu-system-x86_64 -drive /dev/sda";
    DL_CHECK(withFreshEvidence(f) == matching); // Runtime/config divergence cannot authorize READY.
    f.io.texts.erase("/proc/1/cmdline"); f.io.texts.erase("/proc/1/fdinfo/4");
    DL_CHECK(withFreshEvidence(f) == matching); // Missing runtime side channel also cannot authorize it.
    // A real, ordinary open file on the target remains attributable.
    f.io.links["/proc/1/fd/4"].value = "/target/runtime-image";
    f.io.paths["/proc/1/fd/4"].value = OwnershipPath{false,{8,0}, true, false, std::nullopt, false};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Busy);
    // Stopped guests remain assigned even without any running FD.
    f.io.lists["/proc/1/fd"].value->clear();
    DL_CHECK(device(withFreshEvidence(f),"guest-storage").status == DriveStatus::Protected);
}
void socketQueuedRights() {
    Fixture f;
    f.io.lists["/proc/1/fd"].value->push_back("3");
    OwnershipPath socket{false,{0,77},false,false, std::nullopt, false};
    socket.socket = true;
    f.io.paths["/proc/1/fd/3"] = {socket,false};
    f.io.links["/proc/1/fd/3"] = {std::string("socket:[77]"),false};
    for (const auto& [info, ready] : std::vector<std::pair<std::string,bool>>{
        {"pos: 0\nscm_fds: 0\n",true},
        {"pos: 0\n",false}, {"scm_fds: 1\n",false},
        {"scm_fds: -1\n",false}, {"scm_fds: 0junk\n",false},
        {"scm_fds: 0\nscm_fds: 0\n",false},
        {"scm_fds: 0",false}, {"",false}}) {
        f.io.texts["/proc/1/fdinfo/3"] = {info,false};
        DL_CHECK(device(withFreshEvidence(f)).status == (ready ? DriveStatus::Ready : DriveStatus::Unknown));
    }
    f.io.texts["/proc/1/fdinfo/3"] = {std::string("scm_fds: 0\n"),false};
    f.io.paths["/proc/1/fd/3"].value->socket = false; // Name alone is not socket identity.
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.paths["/proc/1/fd/3"].value->socket = true;
    f.io.reads.clear(); f.io.change = "/proc/1/fdinfo/3"; // Second observation unreadable/malformed.
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.change.clear(); f.io.texts["/proc/1/fdinfo/3"] = {std::nullopt,false};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
}
void zfsControlAccounting() {
    Fixture f; f.add("pool-disk",{8,16});
    f.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
    f.io.texts["/sys/class/misc/zfs/dev"] = {std::string("10:249\n"),false};
    f.zfs.result.pools.push_back({"pool","42",{"root","root",{},
        {{"disk","disk","/dev/pool-disk",{},true}},true},true});
    const auto complete = f.zfs.result;
    f.io.lists["/proc/1/fd"].value->push_back("3");
    f.io.paths["/proc/1/fd/3"] = {OwnershipPath{false,{0,7},false,false,BlockDeviceNumber{10,249}, false},false};
    f.io.links["/proc/1/fd/3"] = {std::string("/an/arbitrary/control-alias"),false};
    auto a = withFreshEvidence(f);
    DL_CHECK(device(a).status == DriveStatus::Ready && f.zfs.reads == 2);
    DL_CHECK(a.coverage.at("zfs") && a.coverage.at("processes"));
    DL_CHECK(device(a,"pool-disk").status == DriveStatus::Protected);
    DL_CHECK(reason(device(a,"pool-disk"),OwnershipReasonCode::ZfsMember));
    DL_CHECK(!reason(device(a,"pool-disk"),OwnershipReasonCode::OpenFileHandle));
    // Even the expected pathname does not override a mismatching character ID.
    f.io.links["/proc/1/fd/3"].value = "/dev/zfs";
    f.io.paths["/proc/1/fd/3"].value->character_device = BlockDeviceNumber{10,248};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.paths["/proc/1/fd/3"].value->character_device = BlockDeviceNumber{10,249};
    f.io.texts.erase("/sys/class/misc/zfs/dev");
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.texts["/sys/class/misc/zfs/dev"] = {std::string("10:249\n"),false};
    f.zfs.result = {false,{},"provider unavailable"};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.zfs.result = complete;
    f.zfs.reads = 0; f.zfs.sequence = {complete,{false,{},"failed final capture"}};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    auto changed = complete;
    changed.pools.front().root.children.front().path = "/dev/sda";
    f.zfs.reads = 0; f.zfs.sequence = {complete,changed};
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.zfs.sequence.clear(); f.io.reads.clear(); f.io.change = "/sys/class/misc/zfs/dev";
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
    f.io.change.clear();
    f.zfs.result = {true,{},"complete empty pool inventory"};
    f.io.texts["/sys/class/misc/zfs/dev"].value = "10:251\n";
    f.io.paths["/proc/1/fd/3"].value->character_device = BlockDeviceNumber{10,251};
    a = withFreshEvidence(f); // Dynamic minor plus complete zero-pool captures is supported.
    DL_CHECK(device(a).status == DriveStatus::Ready);
    DL_CHECK(a.coverage.at("zfs") && a.coverage.at("processes"));
    f.io.lists.erase("/sys/module/zfs"); // Inferred no-module coverage is not provider evidence.
    DL_CHECK(device(withFreshEvidence(f)).status == DriveStatus::Unknown);
}
void processCoverage() {
    Fixture f;
    f.io.lists["/proc/1/fd"].value->push_back("3");
    f.io.paths["/proc/1/fd/3"] = {OwnershipPath{true,{8,0}, true, false, std::nullopt, false},false};
    DL_CHECK(device(f.assess()).status == DriveStatus::Busy);
    f.io.paths["/proc/1/fd/3"] = {};
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);
    f.io.lists["/proc/1/fd"].value->clear();
    f.io.links["/proc/1/ns/mnt"].value = "mnt:[2]";
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);
}
}
int main() {
    return drivelab::test::run([] { proxmoxDmShape(); brokenChains(); mdAndZfs(); nestedLvmAndZvol(); processCoverage(); runtimeGuestReferences(); socketQueuedRights(); zfsControlAccounting(); });
}
