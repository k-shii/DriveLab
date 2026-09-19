#include "ownership_fixture.h"
#include "fakes/fresh_probe_api.h"
#include "core/scan_profile.h"
#include <cerrno>
#include <cstring>
using namespace ownership_test;
namespace drivelab::fresh_probe_api {
struct Partition {
    int number = 1;
    std::int64_t start = 8, size = 1024;
    int type = 0x83;
    std::string guid = "0fc63daf-8483-4772-8e79-3d69d8477de4";
    unsigned long long flags = 0;
    bool extended = false;
};
struct Probe { int ordinal; };
}
namespace {
using State = FreshSignatureState;
using Partition = drivelab::fresh_probe_api::Partition;
struct Config {
    BlockDeviceNumber number{8,0};
    std::uint64_t bytes = 1024 * 1024;
    int result = 1, error = 0, open_error = 0, configure_error = 0, close_error = 0;
    bool wrong_device = false, changed_path = false, second_error = false;
    int region_error = 0; bool short_region = false;
    std::map<std::string,std::string> tags;
    std::vector<Partition> parts;
};
std::map<std::string, Config> devices;
Config* current = nullptr;
int opens = 0, closes = 0, probes = 0, freed = 0, ordinal = 0;
bool decoders = true, held = false;
void reset() { devices.clear(); current = nullptr; opens = closes = probes = freed = ordinal = 0; decoders = true; held = false; }
}
namespace drivelab::ownership_io {
int open(const char* path, int flags, ...) {
    DL_CHECK(flags == read_flags && !held); ++opens;
    const auto it = devices.find(path); DL_CHECK(it != devices.end()); current = &it->second;
    if (current->open_error) { errno = current->open_error; return -1; }
    held = true; ordinal = 0; return 42;
}
int close(int fd) { DL_CHECK(fd == 42 && held); held = false; ++closes; errno = EIO; return current->close_error; }
int fstat(int fd, Status* out) {
    DL_CHECK(fd == 42 && held);
    out->st_mode = 0060000;
    out->st_rdev = (static_cast<std::uint64_t>(current->number.major_number) << 32) |
        (current->number.minor_number + (current->wrong_device ? 1U : 0U));
    return 0;
}
int stat(const char*, Status* out) {
    fstat(42, out); if (current->changed_path) ++out->st_rdev; return 0;
}
}
namespace drivelab::fresh_probe_api {
std::ptrdiff_t read_at(int fd, void*, std::size_t size, std::uint64_t offset) {
    DL_CHECK(fd == 42 && held && size <= 64 * 1024);
    if (current->region_error) { errno = current->region_error; return -1; }
    const auto available = offset >= current->bytes ? 0 : std::min<std::uint64_t>(size,current->bytes-offset);
    return static_cast<std::ptrdiff_t>(available) - (current->short_region && available ? 1 : 0);
}
blkid_probe blkid_new_probe() { ++probes; return new Probe{++ordinal}; }
void blkid_free_probe(blkid_probe p) { ++freed; delete p; }
int blkid_probe_set_device(blkid_probe, int fd, std::int64_t offset, std::int64_t size) {
    DL_CHECK(fd == 42 && held && offset == 0 && size == 0); errno = EIO; return current->configure_error;
}
std::int64_t blkid_probe_get_size(blkid_probe) { return static_cast<std::int64_t>(current->bytes); }
int blkid_probe_enable_superblocks(blkid_probe, int yes) { DL_CHECK(yes == 1); return 0; }
int blkid_probe_set_superblocks_flags(blkid_probe, int flags) { DL_CHECK(flags == signature_flags); return 0; }
int blkid_probe_enable_partitions(blkid_probe, int yes) { DL_CHECK(yes == 1); return 0; }
int blkid_probe_enable_topology(blkid_probe, int yes) { DL_CHECK(yes == 0); return 0; }
int blkid_known_fstype(const char*) { return decoders ? 1 : 0; }
int blkid_do_safeprobe(blkid_probe p) {
    if (p->ordinal == 2 && current->second_error) { errno = EIO; return -1; }
    errno = current->error; return current->result;
}
int blkid_probe_lookup_value(blkid_probe, const char* name, const char** data, std::size_t* length) {
    const auto it = current->tags.find(name); if (it == current->tags.end()) return -1;
    *data = it->second.c_str(); *length = it->second.size() + 1; return 0;
}
Probe* blkid_probe_get_partitions(blkid_probe p) { return p; }
int blkid_partlist_numof_partitions(Probe*) { return static_cast<int>(current->parts.size()); }
Partition* blkid_partlist_get_partition(Probe*, int i) { return &current->parts.at(static_cast<std::size_t>(i)); }
int blkid_partition_get_partno(Partition* p) { return p->number; }
std::int64_t blkid_partition_get_start(Partition* p) { return p->start; }
std::int64_t blkid_partition_get_size(Partition* p) { return p->size; }
int blkid_partition_get_type(Partition* p) { return p->type; }
const char* blkid_partition_get_type_string(Partition* p) { return p->guid.c_str(); }
unsigned long long blkid_partition_get_flags(Partition* p) { return p->flags; }
int blkid_partition_is_extended(Partition* p) { return p->extended ? 1 : 0; }
int blkid_partition_is_logical(Partition*) { return 0; }
}
namespace {
class Raw final : public BlockInventoryProvider {
public:
    BlockInventorySnapshot snapshot;
    int reads = 0, change_at = 0;
    Result<BlockInventorySnapshot> scan() override {
        auto out = snapshot;
        if (++reads == change_at) out.devices.front().wwn = "50014ee001234599";
        return Result<BlockInventorySnapshot>::success(std::move(out));
    }
};
struct NativeFixture : Fixture {
    Raw raw;
    NativeFixture() {
        reset();
        node("sda").observation.wwn = "50014ee001234567";
        node("sda").signature = {};
        sync();
    }
    void sync() {
        raw.snapshot = {}; raw.reads = 0;
        for (const auto& b : inventory.nodes) {
            raw.snapshot.devices.push_back(b.observation);
            auto& d = devices[*b.current_path];
            d.number = *b.observation.device_number;
            d.bytes = b.observation.capacity_bytes.value_or(0);
        }
        const auto resolved = resolveBlockIdentities(raw.snapshot);
        for (auto& b : inventory.nodes) {
            const auto it = std::find_if(resolved.nodes.begin(), resolved.nodes.end(),
                [&](const auto& r) { return r.observation.kernel_name == b.observation.kernel_name; });
            DL_CHECK(it != resolved.nodes.end()); b.identity = it->identity;
        }
    }
    OwnershipAssessment assessFresh() { return assessFresh(io); }
    OwnershipAssessment assessFresh(OwnershipIo& observations) {
        NativeFreshSignatureSource fresh(raw, observations);
        LinuxOwnershipSource source(observations, zfs, fresh);
        const auto a = assessOwnership(inventory, source.read(inventory));
        DL_CHECK(!held && freed == probes);
        return a;
    }
};
void blankAndFilesystem() {
    NativeFixture f;
    f.node("sda").issues.push_back({DiscoveryIssueCode::MissingEvidence,"sda","signature","Cache has no signature"});
    f.inventory.issues.push_back(f.node("sda").issues.back());
    auto a = f.assessFresh();
    DL_CHECK(device(a).status == DriveStatus::Ready);
    DL_CHECK(a.fresh_signatures.at("block:sda").state == State::None && probes == 2 && opens == closes);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ChecksComplete));
    devices["/dev/sda"].result = 0;
    devices["/dev/sda"].tags = {{"TYPE","ext4"},{"USAGE","filesystem"}};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.node("sda").signature.type = "ntfs";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
}
void failuresAndMembership() {
    for (const auto state : {State::PermissionDenied, State::ReadFailure, State::Disappeared, State::Conflicting, State::Unsupported}) {
        NativeFixture f; auto& d = devices["/dev/sda"];
        if (state == State::PermissionDenied) d.open_error = EACCES;
        if (state == State::ReadFailure) { d.result = -1; d.error = EIO; }
        if (state == State::Disappeared) d.changed_path = true;
        if (state == State::Conflicting) d.result = -2;
        if (state == State::Unsupported) decoders = false;
        const auto a = f.assessFresh();
        DL_CHECK(a.fresh_signatures.at("block:sda").state == state);
        DL_CHECK(device(a).status == DriveStatus::Unknown);
    }
    for (int mode = 0; mode < 8; ++mode) {
        NativeFixture f; auto& d = devices["/dev/sda"];
        if (mode == 0) { d.result = 1; d.error = EIO; } // A masked read error is NOT an empty disk.
        if (mode == 1) d.second_error = true;
        if (mode == 2) d.close_error = -1;
        if (mode == 3) d.configure_error = -1;
        if (mode == 4) d.wrong_device = true;
        if (mode == 5) d.bytes /= 2;
        if (mode == 6) d.region_error = EIO;
        if (mode == 7) d.short_region = true;
        DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    }
    for (const auto* type : {"LVM2_member","linux_raid_member","zfs_member"}) {
        NativeFixture f; auto& d = devices["/dev/sda"];
        d.result = 0; d.tags = {{"TYPE",type},{"USAGE","raid"}};
        const auto a = f.assessFresh();
        DL_CHECK(a.fresh_signatures.at("block:sda").state == State::Membership);
        DL_CHECK(device(a).status == DriveStatus::Protected);
    }
    NativeFixture f;
    auto e = f.evidence(); // No fresh source injected.
    DL_CHECK(device(assessOwnership(f.inventory,e)).status == DriveStatus::Unknown);
    f.raw.change_at = 2;
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.raw.change_at = 0; f.node("sda").observation.wwn.reset(); f.sync();
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
}
void namespacesAndScope() {
    NativeFixture f;
    f.io.lists["/proc"].value = std::vector<std::string>{"3","1","2"};
    for (const auto* pid : {"2","3"}) {
        f.io.paths[std::string("/proc/")+pid+"/cwd"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        f.io.paths[std::string("/proc/")+pid+"/root"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        f.io.texts[std::string("/proc/")+pid+"/maps"] = {std::string{},false};
        f.io.lists[std::string("/proc/")+pid+"/task"] = {std::vector<std::string>{pid},false};
        f.io.links[std::string("/proc/")+pid+"/ns/mnt"] = {std::string("mnt:[2]"),false};
        f.io.lists[std::string("/proc/")+pid+"/fd"] = {std::vector<std::string>{},false};
    }
    f.io.texts["/proc/2/mountinfo"] = {std::string("2 0 8:0 / /data rw - ext4 /dev/sda rw\n"),false};
    auto a = f.assessFresh();
    DL_CHECK(device(a).status == DriveStatus::Protected && reason(device(a),OwnershipReasonCode::Mount));
    DL_CHECK(a.namespace_mounts.size() == 3 && f.io.reads["/proc/2/mountinfo"] == 4);
    DL_CHECK(f.io.reads["/proc/3/mountinfo"] == 0);
    f.io.texts["/proc/2/mountinfo"].value = "2 0 0:3 / /proc rw - proc proc rw\n";
    a = f.assessFresh();
    DL_CHECK(device(a).status == DriveStatus::Ready);
    std::reverse(f.io.lists["/proc"].value->begin(), f.io.lists["/proc"].value->end());
    DL_CHECK(f.assessFresh() == a);
    f.io.texts.erase("/proc/2/mountinfo");
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/2/mountinfo"].value = "2 0 8:99 / /data rw - ext4 /dev/missing rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/2/mountinfo"].value = "2 0 0:33 / /proc rw - proc proc rw,hidepid=2\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    NativeFixture other;
    other.add("sdb",{8,16}); other.node("sdb").identity = {};
    other.node("sdb").issues.push_back({DiscoveryIssueCode::MissingEvidence,"sdb","identity","No stable identity"});
    other.inventory.issues.push_back(other.node("sdb").issues.back()); other.sync();
    DL_CHECK(device(other.assessFresh()).status == DriveStatus::Ready);
    other.raw.snapshot.issues.push_back({BlockInventoryIssueCode::MalformedAttribute,
        std::string("sdb"),"model","Unrelated raw model observation"});
    DL_CHECK(device(other.assessFresh()).status == DriveStatus::Ready);
    other.raw.snapshot.issues.back().kernel_name.reset();
    DL_CHECK(device(other.assessFresh()).status == DriveStatus::Unknown);
    other.raw.snapshot.issues.clear();
    other.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
    other.zfs.result = {false,{},"ZFS unavailable"};
    DL_CHECK(device(other.assessFresh()).status == DriveStatus::Unknown);
}
void retainedProcessUse() {
    NativeFixture f;
    f.io.lists["/proc/1/fd"].value = std::vector<std::string>{"4"};
    f.io.paths["/proc/1/fd/4"] = {OwnershipPath{false,{8,0}, true, false, std::nullopt, false},false};
    f.io.links["/proc/1/fd/4"] = {std::string("/data/open (deleted)"),false};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Busy);
    f.io.paths["/proc/1/fd/4"].value->device = {0,99};
    f.io.links["/proc/1/fd/4"].value = "mnt:[99]";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.lists["/proc/1/fd"].value->clear();
    f.io.texts["/proc/1/maps"].value = "1000-2000 r--p 00000000 08:00 123 /data/mapped (deleted)\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Busy);
    f.io.texts["/proc/1/maps"].value = "1000-2000 rw-p 00000000 00:00 0 [heap]\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.texts.erase("/proc/1/maps");
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    NativeFixture cwd;
    cwd.io.paths["/proc/1/cwd"].value->device = {8,0};
    DL_CHECK(device(cwd.assessFresh()).status == DriveStatus::Busy);
    cwd.io.paths.erase("/proc/1/cwd"); cwd.io.paths.erase("/proc/1/root");
    DL_CHECK(device(cwd.assessFresh()).status == DriveStatus::Unknown);
    cwd.io.texts["/proc/1/status"] = {std::string("Kthread: 1\n"),false};
    DL_CHECK(device(cwd.assessFresh()).status == DriveStatus::Ready);
    NativeFixture thread;
    thread.io.paths["/proc/1/task/2/cwd"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
    thread.io.paths["/proc/1/task/2/root"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
    thread.io.lists["/proc/1/task"].value->push_back("2");
    thread.io.links["/proc/1/task/2/ns/mnt"] = {std::string("mnt:[2]"),false};
    thread.io.texts["/proc/1/task/2/mountinfo"] = {std::string("2 0 8:0 / /data rw - ext4 /dev/sda rw\n"),false};
    thread.io.texts["/proc/1/task/2/maps"] = {std::string{},false};
    thread.io.lists["/proc/1/task/2/fd"] = {std::vector<std::string>{},false};
    DL_CHECK(device(thread.assessFresh()).status == DriveStatus::Protected);
    thread.io.texts.erase("/proc/1/task/2/mountinfo");
    DL_CHECK(device(thread.assessFresh()).status == DriveStatus::Unknown);
}
void procfsVisibilityAndScoping() {
    NativeFixture f;
    const auto baseline = *f.io.texts["/proc/self/mountinfo"].value;
    *f.io.texts["/proc/self/mountinfo"].value +=
        "3 1 0:30 / /isolated/proc rw - proc proc rw,hidepid=2,subset=pid\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    for (const auto* option : {"hidepid=0","hidepid=off","subset=pid"}) {
        f.io.texts["/proc/self/mountinfo"].value = baseline.substr(0,baseline.size()-1) + "," + option + "\n";
        DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    }
    for (const auto* option : {"hidepid=1","hidepid=2","hidepid=4","hidepid=invisible","hidepid=unexpected"}) {
        f.io.texts["/proc/self/mountinfo"].value = baseline.substr(0,baseline.size()-1) + "," + option + "\n";
        const auto a = f.assessFresh();
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(std::any_of(a.coverage_gaps.at("processes").begin(),a.coverage_gaps.at("processes").end(),
            [](const auto& r) { return r.node.empty() && r.detail.find("Restricted procfs visibility: scanned /proc") != std::string::npos; }));
    }
    f.io.texts["/proc/self/mountinfo"].value = baseline +
        "3 2 0:77 / /proc/123 rw - tmpfs tmpfs rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/self/mountinfo"].value = baseline +
        "3 2 0:77 / /proc/sys rw - tmpfs tmpfs rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.texts["/proc/self/mountinfo"].value = baseline;
    f.io.paths.erase("/proc");
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
}
void realHostShape() {
    NativeFixture f;
    f.add("system",{8,16}); f.node("system").observation.wwn = "50014ee001234568";
    f.node("system").signature = {};
    f.dm("dm-0",0,"system"); f.dm("dm-1",1,"system");
    f.mount("dm-1","/"); f.swap("dm-0");
    f.add("guest-disk",{8,32}); // Endpoint identity unavailable, structurally separable.
    f.pve(); f.guest("scsi0: /dev/dm-1\nscsi1: /dev/guest-disk\n");
    f.io.texts["/etc/pve/storage.cfg"].value = "lvmthin: local\n vgname pve\n thinpool data\n";
    f.io.texts["/proc/self/mountinfo"].value =
        "1 0 253:1 / / rw - ext4 /dev/dm-1 rw\n"
        "2 1 0:3 / /proc rw - proc proc rw\n"
        "3 1 0:1 / /tmp rw - tmpfs tmpfs rw\n";
    f.io.paths["/proc/1/cwd"].value->device = {253,1};
    f.io.paths["/proc/1/root"].value->device = {253,1};
    f.io.lists["/proc"].value->push_back("2");
    f.io.lists["/proc/2/task"] = {std::vector<std::string>{"2"},false};
    f.io.lists["/proc/2/fd"] = {std::vector<std::string>{},false};
    f.io.links["/proc/2/ns/mnt"] = {std::string("mnt:[2]"),false};
    f.io.paths["/proc/2/cwd"] = {OwnershipPath{false,{253,1}, true, false, std::nullopt, false},false};
    f.io.paths["/proc/2/root"] = {OwnershipPath{false,{253,1}, true, false, std::nullopt, false},false};
    f.io.texts["/proc/2/maps"] = {std::string{},false};
    f.io.texts["/proc/2/mountinfo"] = {std::string(
        "1 0 253:1 / / rw - ext4 /dev/dm-1 rw\n"
        "2 1 0:13 / /proc rw - proc proc rw,hidepid=2,subset=pid\n"
        "3 1 0:14 / /sys rw - sysfs sysfs rw\n"
        "4 1 0:15 / /tmp rw - tmpfs tmpfs rw\n"
        "5 1 0:16 / /sys/fs/cgroup rw - cgroup2 cgroup rw\n"
        "6 1 0:17 / /dev rw - devtmpfs udev rw\n"
        "7 1 0:18 / /run/credentials rw - ramfs ramfs rw\n"
        "9 1 0:19 / /sys/firmware/efi/efivars rw - efivarfs efivarfs rw\n"),false};
    for (int i=3;i<20;++i) {
        const auto fd = std::to_string(i), path = "/proc/1/fd/" + fd;
        f.io.lists["/proc/1/fd"].value->push_back(fd);
        f.io.paths[path] = {OwnershipPath{false,{253,1}, true, false, std::nullopt, false},false};
        f.io.links[path] = {"/root/file" + fd,false};
        *f.io.texts["/proc/1/maps"].value += fd + "000-" + fd + "800 r--p 00000000 fd:01 " + fd + " /root/file" + fd + "\n";
    }
    f.io.links.erase("/proc/1/fd/3"); // stat still proves the unrelated root backing.
    f.sync();
    auto& member = devices["/dev/system"];
    member.result=0; member.tags={{"TYPE","LVM2_member"},{"USAGE","raid"}};
    const auto clean = f.assessFresh();
    DL_CHECK(!clean.coverage.at("processes"));
    DL_CHECK(device(clean).status == DriveStatus::Ready);
    DL_CHECK(device(clean,"system").status == DriveStatus::Protected);
    DL_CHECK(reason(device(clean,"system"),OwnershipReasonCode::SystemRoot));
    DL_CHECK(reason(device(clean,"system"),OwnershipReasonCode::ActiveSwap));
    DL_CHECK(reason(device(clean,"system"),OwnershipReasonCode::GuestAssignment));
    DL_CHECK(!trace(clean,"pve-guest:qemu-server:100").complete);
    DL_CHECK(!reason(device(clean),OwnershipReasonCode::IncompleteCoverage));
    DL_CHECK(clean.coverage_gaps.at("processes").size() == 1);
    DL_CHECK(clean.coverage_gaps.at("processes").front().node == "block:dm-1");
    std::reverse(f.io.lists["/proc"].value->begin(),f.io.lists["/proc"].value->end());
    DL_CHECK(f.assessFresh() == clean);
    const auto mounts = *f.io.texts["/proc/2/mountinfo"].value;
    *f.io.texts["/proc/2/mountinfo"].value += "8 1 253:1 bad-root /bad rw - ext4 /dev/dm-1 rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.texts["/proc/2/mountinfo"].value = mounts + "malformed unaddressed mount\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/2/mountinfo"].value = mounts;
    const auto maps = *f.io.texts["/proc/1/maps"].value;
    *f.io.texts["/proc/1/maps"].value += "bad-range r--p 0000 fd:01 99 /root/malformed\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.texts["/proc/1/maps"].value = maps + "unaddressed mapping\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/1/maps"].value = maps + "3000-4000 r--p 0000 fd:01 99 /root/truncated";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/1/maps"].value = maps;
    f.io.texts["/proc/2/mountinfo"].value = mounts + "8 1 253:1 / /truncated rw - ext4 /dev/dm-1 rw";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/2/mountinfo"].value = mounts;
    // A regular devtmpfs file and native /dev/null are not opaque devices.
    f.io.paths["/proc/1/fd/3"].value = OwnershipPath{false,{0,17}, true, false, std::nullopt, false};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.paths["/proc/1/fd/3"].value = OwnershipPath{false,{0,17},false,true, std::nullopt, false};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    // An arbitrary character/control device cannot be scoped by devtmpfs st_dev.
    f.io.paths["/proc/1/fd/3"].value->known_nonstorage = false;
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.paths["/proc/1/fd/3"].value = OwnershipPath{false,{0,50}, true, false, std::nullopt, false};
    f.io.links["/proc/1/fd/3"] = {std::string("anon_inode:[io_uring]"),false};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.paths["/proc/1/fd/3"].value = OwnershipPath{false,{253,1}, true, false, std::nullopt, false};
    f.io.links["/proc/1/fd/3"].value = "/root/file3";
    *f.io.texts["/proc/2/mountinfo"].value +=
        "8 1 0:99 / /opaque rw - overlay overlay rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/proc/2/mountinfo"].value = mounts +
        "8 1 0:99 / /fuse rw - fuse arbitrary-source rw\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
}
void addPseudoMount(NativeFixture& f, const std::string& row) {
    *f.io.texts["/proc/self/mountinfo"].value += row;
    ResolvedInventorySnapshot parsed; parseMountInfo(row,parsed);
    DL_CHECK(parsed.issues.empty() && parsed.mounts.size() == 1);
    // Preserve B's real UncorrelatedUsage too; the later Core gate must agree
    // with C's namespace scoping, without relaxing other B issue types.
    f.inventory.mounts.push_back(parsed.mounts.front());
    DiscoveryIssue issue{DiscoveryIssueCode::UncorrelatedUsage,"","mountinfo","No block owner"};
    issue.mount_id = parsed.mounts.front().mount_id;
    f.inventory.issues.push_back(issue);
}
void pseudoMountsAndHandles() {
    for (const auto& [row, ready] : std::vector<std::pair<std::string,bool>>{
        {"90 1 0:70 / /var/lib/lxcfs rw - fuse.lxcfs lxcfs rw\n",true},
        {"90 1 0:70 /proc/meminfo /proc/meminfo rw - fuse.lxcfs lxcfs rw\n",true},
        {"90 1 0:70 / /var/lib/lxcfs rw - fuse.lxcfs other rw\n",false},
        {"90 1 0:70 / /var/lib/lxcfs rw - fuse.other lxcfs rw\n",false},
        {"90 1 0:70 / /data rw - fuse /dev/fuse rw\n",false},
        {"90 1 0:70 / /etc/pve-like rw - fuse /dev/fuse rw\n",false},
        {"90 1 0:70 / /etc/pve rw - fuse arbitrary-source rw\n",false},
        {"90 1 0:70 / /etc/pve rw - fuse.other /dev/fuse rw\n",false},
        {"90 1 0:70 /subtree /etc/pve rw - fuse /dev/fuse rw\n",false}}) {
        NativeFixture f; addPseudoMount(f,row);
        f.io.lists["/proc/1/fd"].value->push_back("3");
        f.io.paths["/proc/1/fd/3"] = {OwnershipPath{false,{0,70}, true, false, std::nullopt, false},false};
        f.io.links["/proc/1/fd/3"] = {std::string("/virtual/file"),false};
        const auto a = f.assessFresh();
        DL_CHECK(device(a).status == (ready ? DriveStatus::Ready : DriveStatus::Unknown));
        DL_CHECK(a.coverage.at("processes") == ready);
        if (!ready) DL_CHECK(std::any_of(a.coverage_gaps.at("processes").begin(),a.coverage_gaps.at("processes").end(),
            [](const auto& gap) { return gap.node.empty(); }));
    }
    for (const auto& [target, ready] : std::vector<std::pair<std::string,bool>>{
        {"anon_inode:[pidfd]",true}, {"anon_inode:bpf-prog",true},
        {"anon_inode:pidfd",true}, {"anon_inode:[bpf-prog]",true},
        {"anon_inode:[eventfd]",true}, {"anon_inode:inotify",true},
        {"anon_inode:[io_uring]",false}, {"anon_inode:[fanotify]",false},
        {"anon_inode:[unknown]",false}, {"anon_inode:",false},
        {"anon_inode:[pidfd",false}, {"anon_inode:pidfd]",false},
        {"anon_inode:[pidfd]extra",false}, {"anon_inode:[bpf-prog ]",false},
        {"opaque-kernel-object",false}, {"/unknown/ordinary-file",false}}) {
        NativeFixture f;
        f.io.lists["/proc/1/fd"].value->push_back("3");
        f.io.paths["/proc/1/fd/3"] = {OwnershipPath{false,{0,70},false,false, std::nullopt, false},false};
        f.io.links["/proc/1/fd/3"] = {target,false};
        auto a = f.assessFresh();
        DL_CHECK(device(a).status == (ready ? DriveStatus::Ready : DriveStatus::Unknown));
        DL_CHECK(a.coverage.at("processes") == ready);
        f.io.links.erase("/proc/1/fd/3");
        DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    }
}
void pmxcfsBacking() {
    NativeFixture f;
    f.add("system",{8,16}); f.node("system").observation.wwn = "50014ee001234568";
    f.node("system").signature = {};
    f.dm("dm-0",0,"system"); f.dm("dm-1",1,"system");
    f.mount("dm-1","/"); f.swap("dm-0"); f.pve();
    f.guest("scsi0: /dev/dm-1\n");
    f.io.texts["/proc/self/mountinfo"].value =
        "1 0 253:1 / / rw - ext4 /dev/dm-1 rw\n2 1 0:3 / /proc rw - proc proc rw\n";
    addPseudoMount(f,"90 1 0:70 / /etc/pve rw - fuse /dev/fuse rw\n");
    f.io.paths["/etc/pve"] = {OwnershipPath{false,{0,70}, true, false, std::nullopt, false},false};
    f.io.paths["/var/lib/pve-cluster/config.db"] = {OwnershipPath{false,{253,1}, true, false, std::nullopt, false},false};
    for (const auto& [fd,dev,target] : std::vector<std::tuple<std::string,BlockDeviceNumber,std::string>>{
        {"3",{0,70},"/etc/pve/storage.cfg"},
        {"4",{253,1},"/var/lib/pve-cluster/config.db"},
        {"5",{0,71},"anon_inode:[pidfd]"},
        {"6",{0,71},"anon_inode:bpf-prog"}}) {
        f.io.lists["/proc/1/fd"].value->push_back(fd);
        f.io.paths["/proc/1/fd/"+fd] = {OwnershipPath{false,dev, true, false, std::nullopt, false},false};
        f.io.links["/proc/1/fd/"+fd] = {target,false};
    }
    f.io.paths["/proc/1/root"].value->device = {253,1};
    f.io.paths["/proc/1/cwd"].value->device = {253,1};
    f.sync();
    devices["/dev/system"].result = 0;
    devices["/dev/system"].tags = {{"TYPE","LVM2_member"},{"USAGE","raid"}};
    const auto clean = f.assessFresh();
    DL_CHECK(device(clean).status == DriveStatus::Ready && clean.coverage.at("processes"));
    const auto system = device(clean,"system");
    DL_CHECK(system.status == DriveStatus::Protected);
    for (const auto code : {OwnershipReasonCode::SystemRoot,OwnershipReasonCode::ActiveSwap,
         OwnershipReasonCode::LvmMember,OwnershipReasonCode::GuestAssignment,
         OwnershipReasonCode::ConfiguredStorage,OwnershipReasonCode::OpenFileHandle})
        DL_CHECK(reason(system,code));
    for (const auto* source : {"pmxcfs:database","/proc/1/fd/3","/proc/1/fd/4"})
        DL_CHECK(std::any_of(clean.claims.begin(),clean.claims.end(),[&](const auto& claim) {
            return claim.consumer == "block:dm-1" && claim.source == source;
        }));
    DL_CHECK(f.assessFresh() == clean);
    // Missing/opaque database backing, unknown instance and missing host identity
    // cannot be excused by the recognized FUSE mount spelling.
    f.io.paths.erase("/var/lib/pve-cluster/config.db");
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.paths["/var/lib/pve-cluster/config.db"] = {OwnershipPath{false,{0,99}, true, false, std::nullopt, false},false};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.paths["/var/lib/pve-cluster/config.db"].value = OwnershipPath{false,{253,1}, true, false, std::nullopt, false};
    f.io.paths["/etc/pve"].value->device = {0,99};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.paths["/etc/pve"].value->device = {0,70};
    const auto local = f.io.links.at("/etc/pve/local"); f.io.links.erase("/etc/pve/local");
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.links["/etc/pve/local"] = local;
    // When the target actually backs pmxcfs, it cannot be READY.
    f.io.paths["/var/lib/pve-cluster/config.db"].value->device = {8,0};
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Protected);
    f.io.paths["/var/lib/pve-cluster/config.db"].value->device = {253,1};
    f.io.reads.clear(); f.io.disappear_location = "/var/lib/pve-cluster/config.db";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
}
// Inject complete read outcomes, including absence versus permission failures.
// A sequence falls back to the fixture after its final explicitly scripted read.
class ProcessSequence final : public OwnershipIo {
public:
    explicit ProcessSequence(Io& fixture) : fixture_(fixture) {}
    std::map<std::string,std::vector<OwnershipRead<std::string>>> text_steps, link_steps;
    std::map<std::string,std::vector<OwnershipRead<std::vector<std::string>>>> list_steps;
    std::map<std::string,std::vector<OwnershipRead<OwnershipPath>>> path_steps;
    std::map<std::string,std::size_t> reads;
    OwnershipRead<std::string> text(const std::string& path) override {
        if (const auto value = next(text_steps,path,"text:")) return *value;
        return fixture_.text(path);
    }
    OwnershipRead<std::vector<std::string>> list(const std::string& path) override {
        if (const auto value = next(list_steps,path,"list:")) return *value;
        return fixture_.list(path);
    }
    OwnershipRead<std::string> canonical(const std::string& path) override {
        if (const auto value = next(link_steps,path,"link:")) return *value;
        return fixture_.canonical(path);
    }
    OwnershipRead<OwnershipPath> locate(const std::string& path) override {
        if (const auto value = next(path_steps,path,"stat:")) return *value;
        return fixture_.locate(path);
    }
private:
    template<class T> std::optional<OwnershipRead<T>> next(
        const std::map<std::string,std::vector<OwnershipRead<T>>>& steps,
        const std::string& path, const char* prefix) {
        const auto count = reads[std::string(prefix) + path]++;
        const auto found = steps.find(path);
        if (found != steps.end() && count < found->second.size()) return found->second[count];
        return std::nullopt;
    }
    Io& fixture_;
};
void processRetrySnapshot() {
    using Names = OwnershipRead<std::vector<std::string>>;
    const Names one{std::vector<std::string>{"1"},false};
    const Names two{std::vector<std::string>{"1","2"},false};
    auto task = [](NativeFixture& f, const std::string& pid, const std::string& ns = "mnt:[1]") {
        const auto base = "/proc/" + pid;
        f.io.lists[base + "/task"] = {std::vector<std::string>{pid},false};
        f.io.lists[base + "/fd"] = {std::vector<std::string>{},false};
        f.io.links[base + "/ns/mnt"] = {ns,false};
        f.io.texts[base + "/maps"] = {std::string{},false};
        f.io.paths[base + "/cwd"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        f.io.paths[base + "/root"] = {OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
    };
    auto hasGap = [](const OwnershipAssessment& a, const std::string& part) {
        const auto& gaps = a.coverage_gaps.at("processes");
        return std::any_of(gaps.begin(),gaps.end(),[&](const auto& gap) {
            return gap.detail.find(part) != std::string::npos;
        });
    };
    {
        // The initial task vanishes before namespace/maps/fd collection. A
        // complete retry, not ENOENT itself, establishes the final snapshot.
        NativeFixture f; ProcessSequence io(f.io);
        f.io.lists["/proc/2/task"] = {std::vector<std::string>{"2"},false};
        io.list_steps["/proc"] = {two,one};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Ready);
        DL_CHECK(a.coverage.at("processes") && a.coverage_gaps.at("processes").empty());
        DL_CHECK(io.reads["list:/proc"] == 6); // Retry + second fresh-probe bracket.
    }
    {
        // A newly visible task must be inspected, including candidate ownership.
        NativeFixture f; task(f,"2"); ProcessSequence io(f.io);
        f.io.lists["/proc"] = two;
        f.io.lists["/proc/2/fd"] = {std::vector<std::string>{"4"},false};
        f.io.paths["/proc/2/fd/4"] = {OwnershipPath{true,{8,0}, true, false, std::nullopt, false},false};
        io.list_steps["/proc"] = {one,two};
        DL_CHECK(device(f.assessFresh(io)).status == DriveStatus::Busy);
    }
    {
        // Persistent permission failures are not disappearance and exhaust the
        // finite collection budget in both brackets around fresh probing.
        NativeFixture f; ProcessSequence io(f.io);
        f.io.lists["/proc/1/fd"] = {std::nullopt,false};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(hasGap(a,"File descriptor enumeration incomplete"));
        DL_CHECK(io.reads["list:/proc"] == 12);
    }
    {
        // A failed first bracket cannot be erased by a successful later bracket.
        NativeFixture f; ProcessSequence io(f.io);
        io.list_steps["/proc"] = {one,two,one,two,one,two};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(hasGap(a,"Process set changed during scan"));
        DL_CHECK(io.reads["list:/proc"] == 8);
    }
    {
        // Namespace retry selects a surviving representative; no namespace is
        // excused merely because its original representative disappeared.
        NativeFixture f; task(f,"2","mnt:[2]"); task(f,"3","mnt:[2]");
        ProcessSequence io(f.io);
        const auto mount = std::string("3 1 0:33 / /view rw - tmpfs tmpfs rw\n");
        f.io.texts["/proc/3/mountinfo"] = {mount,false};
        f.io.lists["/proc"] = {std::vector<std::string>{"1","3"},false};
        io.list_steps["/proc"] = {{std::vector<std::string>{"1","2","3"},false},f.io.lists.at("/proc")};
        io.text_steps["/proc/2/mountinfo"] = {{mount,false},{std::nullopt,true}};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Ready);
        DL_CHECK(a.coverage.at("processes"));
        DL_CHECK(io.reads["text:/proc/3/mountinfo"] == 4);
    }
    for (const bool opaque : {false,true}) {
        // Semantic uncertainty survives a settled retry, even after its task
        // disappears. Neither malformed mappings nor io_uring are race gaps.
        NativeFixture f; task(f,"2"); ProcessSequence io(f.io);
        io.list_steps["/proc"] = {two,one};
        if (opaque) {
            f.io.lists["/proc/2/fd"] = {std::vector<std::string>{"4"},false};
            f.io.paths["/proc/2/fd/4"] = {OwnershipPath{false,{0,17}, true, false, std::nullopt, false},false};
            f.io.links["/proc/2/fd/4"] = {std::string("anon_inode:[io_uring]"),false};
        } else f.io.texts["/proc/2/maps"] = {std::string("malformed\n"),false};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(hasGap(a,opaque ? "io_uring" : "Malformed process mapping"));
        DL_CHECK(io.reads["list:/proc"] == 6);
    }
    {
        // An observed block descriptor remains positive evidence if it closes
        // between the paired stat calls and the settled retry has no such FD.
        NativeFixture f; ProcessSequence io(f.io);
        io.list_steps["/proc/1/fd"] = {{std::vector<std::string>{"4"},false}};
        io.path_steps["/proc/1/fd/4"] = {{OwnershipPath{true,{8,0}, true, false, std::nullopt, false},false},{std::nullopt,true}};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Busy);
        DL_CHECK(reason(device(a),OwnershipReasonCode::OpenBlockHandle));
        DL_CHECK(a.coverage.at("processes"));
    }
}
void finalQualificationBoundaries() {
    auto task = [](NativeFixture& f, const std::string& base, const std::string& ns) {
        f.io.lists[base + "/fd"] = {std::vector<std::string>{},false};
        f.io.texts[base + "/maps"] = {std::string{},false};
        f.io.links[base + "/ns/mnt"] = {ns,false};
        f.io.paths[base + "/cwd"] = {OwnershipPath{false,{0,1},true,false,std::nullopt,false},false};
        f.io.paths[base + "/root"] = {OwnershipPath{false,{0,1},true,false,std::nullopt,false},false};
    };
    auto gap = [](const OwnershipAssessment& a, const std::string& detail) {
        const auto& gaps = a.coverage_gaps.at("processes");
        return std::any_of(gaps.begin(),gaps.end(),[&](const auto& g) {
            return g.node.empty() && g.detail == detail;
        });
    };
    const std::string pseudo = "3 1 0:33 / /view rw - tmpfs tmpfs rw\n";
    for (const bool truncated : {false,true}) {
        NativeFixture f;
        task(f,"/proc/2","mnt:[2]");
        f.io.lists["/proc"] = {std::vector<std::string>{"1","2"},false};
        f.io.lists["/proc/2/task"] = {std::vector<std::string>{"2"},false};
        const std::string input = truncated ? pseudo + "4 1 8:0" : "";
        f.io.texts["/proc/2/mountinfo"] = {input,false};
        const auto a = f.assessFresh();
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(gap(a,(truncated ? "Truncated" : "Empty") +
            std::string(" mount namespace mnt:[2] path=/proc/2/mountinfo bytes=") + std::to_string(input.size())));
        // An incomplete tail must not fabricate an attributable mount.
        DL_CHECK(!reason(device(a),OwnershipReasonCode::Mount));
    }
    {
        // A complete preceding block mount still protects its owner even when
        // the final row is truncated; missing rows remain globally unresolved.
        NativeFixture f; task(f,"/proc/2","mnt:[2]");
        f.io.lists["/proc"] = {std::vector<std::string>{"1","2"},false};
        f.io.lists["/proc/2/task"] = {std::vector<std::string>{"2"},false};
        f.io.texts["/proc/2/mountinfo"] = {std::string("3 1 8:0 / /data rw - ext4 /dev/sda rw\n4"),false};
        const auto a = f.assessFresh();
        DL_CHECK(device(a).status == DriveStatus::Protected);
        DL_CHECK(reason(device(a),OwnershipReasonCode::Mount));
        DL_CHECK(!a.coverage.at("processes"));
    }
    {
        // A same-namespace replacement may expose a different chroot-filtered
        // subtree. Disappearance plus a nonempty view cannot erase the empty one.
        NativeFixture f; task(f,"/proc/2","mnt:[2]"); task(f,"/proc/3","mnt:[2]");
        for (const auto* pid : {"2","3"})
            f.io.lists[std::string("/proc/") + pid + "/task"] = {std::vector<std::string>{pid},false};
        f.io.texts["/proc/2/mountinfo"] = {std::string{},false};
        f.io.texts["/proc/3/mountinfo"] = {pseudo,false};
        f.io.lists["/proc"] = {std::vector<std::string>{"1","3"},false};
        ProcessSequence io(f.io);
        io.list_steps["/proc"] = {{std::vector<std::string>{"1","2","3"},false},f.io.lists.at("/proc")};
        const auto a = f.assessFresh(io);
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(gap(a,"Empty mount namespace mnt:[2] path=/proc/2/mountinfo bytes=0"));
        DL_CHECK(io.reads["text:/proc/3/mountinfo"] == 4);
    }
    {
        // Threads can have private FD tables. The leader's empty table cannot
        // authorize skipping a secondary thread's candidate block reference.
        NativeFixture f; const std::string base = "/proc/1/task/2";
        task(f,base,"mnt:[1]");
        f.io.lists["/proc/1/task"] = {std::vector<std::string>{"1","2"},false};
        DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
        f.io.lists[base + "/fd"] = {std::vector<std::string>{"4"},false};
        f.io.paths[base + "/fd/4"] = {OwnershipPath{true,{8,0},false,false,std::nullopt,false},false};
        const auto a = f.assessFresh();
        DL_CHECK(device(a).status == DriveStatus::Busy);
        DL_CHECK(reason(device(a),OwnershipReasonCode::OpenBlockHandle));
        std::reverse(f.io.lists["/proc/1/task"].value->begin(),f.io.lists["/proc/1/task"].value->end());
        DL_CHECK(f.assessFresh() == a);
    }
    {
        // Complete provider evidence is not negative authority over a ring's
        // retained ordinary files, including UserFiles: 0 and empty queues.
        NativeFixture f;
        f.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
        f.zfs.result = {true,{},"Complete empty imported-pool inventory"};
        DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
        f.io.lists["/proc/1/fd"] = {std::vector<std::string>{"4"},false};
        f.io.paths["/proc/1/fd/4"] = {OwnershipPath{false,{0,17},false,false,std::nullopt,false},false};
        f.io.links["/proc/1/fd/4"] = {std::string("anon_inode:[io_uring]"),false};
        f.io.texts["/proc/1/fdinfo/4"] = {std::string("UserFiles:\t0\nSqHead:\t0\nSqTail:\t0\nCqHead:\t0\nCqTail:\t0\n"),false};
        const auto a = f.assessFresh();
        DL_CHECK(a.coverage.at("zfs"));
        DL_CHECK(device(a).status == DriveStatus::Unknown);
        DL_CHECK(a.fresh_signatures.at("block:sda").state == State::None);
    }
}


void repeatedMapsRemainIndependent() {
    NativeFixture f;
    const std::string mapping = "1000-2000 r--p 00000000 08:00 44 /mapped-file\n";
    f.io.texts["/proc/1/maps"] = {mapping,false};
    f.io.lists["/proc/1/task"] = {std::vector<std::string>{"1","2"},false};
    const std::string base = "/proc/1/task/2";
    f.io.links[base+"/ns/mnt"] = {std::string("mnt:[1]"),false};
    f.io.texts[base+"/maps"] = {mapping,false};
    f.io.lists[base+"/fd"] = {std::vector<std::string>{},false};
    f.io.paths[base+"/cwd"] = f.io.paths.at("/proc/1/cwd");
    f.io.paths[base+"/root"] = f.io.paths.at("/proc/1/root");
    ProcessSequence io(f.io);
    ScanProfile profile;
    OwnershipAssessment result;
    {
        ScanProfileSession profiling(&profile);
        result = f.assessFresh(io);
    }
    DL_CHECK(device(result).status == DriveStatus::Busy);
    DL_CHECK(profile.stages.at("ownership.process_collection").calls == 2);
    DL_CHECK(profile.stages.at("ownership.process_pass").calls == 2);
    DL_CHECK(profile.counts.at("process.maps_parse_cache_hits") == 6);
    DL_CHECK(io.reads["text:/proc/1/maps"] == 4 && io.reads["text:"+base+"/maps"] == 4);
    for (const auto& path : {std::string("proc:maps:1:1"),std::string("proc:maps:1:2")})
        DL_CHECK(std::any_of(result.claims.begin(),result.claims.end(),[&](const auto& c) {
            return c.source == path && c.use == OwnershipUse::OpenFileHandle;
        }));
    // Identical malformed inputs cannot share source-specific errors.
    const std::string malformed = "1000-2000 bad 0 08:00 44 /mapped-file\n";
    f.io.texts["/proc/1/maps"].value = malformed;
    f.io.texts[base+"/maps"].value = malformed;
    const auto failed = f.assessFresh();
    DL_CHECK(device(failed).status != DriveStatus::Ready);
    for (const auto& path : {std::string("/proc/1"),base})
        DL_CHECK(std::any_of(failed.coverage_gaps.at("processes").begin(),
            failed.coverage_gaps.at("processes").end(),[&](const auto& gap) {
                return gap.detail.find("Malformed process mapping: "+path+" record=") != std::string::npos;
            }));
    // A changed second read still produces SnapshotChanged/coverage failure.
    f.io.texts["/proc/1/maps"].value = mapping;
    f.io.texts[base+"/maps"].value = mapping;
    ProcessSequence changed(f.io);
    changed.text_steps[base+"/maps"] = {{mapping,false},{std::string{},false}};
    const auto changed_result = f.assessFresh(changed);
    DL_CHECK(reason(device(changed_result),OwnershipReasonCode::OpenFileHandle));
    DL_CHECK(std::any_of(changed_result.coverage_gaps.at("processes").begin(),
        changed_result.coverage_gaps.at("processes").end(),[](const auto& gap) {
            return gap.detail.find("Incomplete file observation: Changed proc:maps:1:2") != std::string::npos;
        }));
}

void deviceKindProbeScope() {
    for (const auto kind : {PhysicalDeviceKind::Optical,PhysicalDeviceKind::Floppy,PhysicalDeviceKind::UnknownPhysical}) {
        NativeFixture f;
        f.node("sda").observation.physical_kind = kind;
        f.partition("sda1","sda",{8,1});
        f.sync();
        NativeFreshSignatureSource source(f.raw,f.io,PhysicalAssessmentScope::Disks);
        const auto result = source.read(f.inventory);
        DL_CHECK(result.at("block:sda").state == State::Unsupported);
        DL_CHECK(result.at("block:sda1").state == State::Unsupported);
        DL_CHECK(opens == 0 && probes == 0 && f.raw.reads == 2);
    }
    NativeFixture f;
    f.node("sda").observation.physical_kind = PhysicalDeviceKind::Disk;
    f.sync();
    NativeFreshSignatureSource source(f.raw,f.io,PhysicalAssessmentScope::Disks);
    DL_CHECK(source.read(f.inventory).at("block:sda").state == State::None);
    DL_CHECK(opens > 0 && probes == 2 && f.raw.reads == 2);
}

void partitionsAndLoops() {
    NativeFixture f; f.partition("sda1","sda",{8,1});
    f.node("sda1").observation.capacity_bytes = 512 * 1024;
    f.node("sda1").signature = {};
    f.io.texts["/sys/class/block/sda1/start"] = {std::string("8\n"),false};
    f.sync();
    auto& d = devices["/dev/sda"]; d.result = 0; d.tags["PTTYPE"] = "gpt"; d.parts.push_back({});
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Ready);
    f.io.texts["/sys/class/block/sda1/start"].value = "9\n";
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    f.io.texts["/sys/class/block/sda1/start"].value = "8\n";
    devices["/dev/sda1"].open_error = EACCES;
    DL_CHECK(device(f.assessFresh()).status == DriveStatus::Unknown);
    NativeFixture loop;
    loop.add("loop0",{7,0},true); loop.node("loop0").observation.capacity_bytes = 0;
    loop.io.texts["/sys/class/block/loop0/size"] = {std::string("0\n"),false}; loop.sync();
    DL_CHECK(device(loop.assessFresh()).status == DriveStatus::Ready);
    loop.io.lists["/sys/class/block/loop0/loop"] = {std::vector<std::string>{"backing_file"},false};
    DL_CHECK(device(loop.assessFresh()).status == DriveStatus::Unknown);
}
}
int main() {
    return drivelab::test::run([] { blankAndFilesystem(); failuresAndMembership(); namespacesAndScope(); retainedProcessUse(); procfsVisibilityAndScoping(); realHostShape(); pseudoMountsAndHandles(); pmxcfsBacking(); processRetrySnapshot(); finalQualificationBoundaries(); partitionsAndLoops(); deviceKindProbeScope(); repeatedMapsRemainIndependent(); });
}
