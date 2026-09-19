#include "platform/linux/linux_ownership_source.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <limits>
#include <memory>
#include <set>
#include <type_traits>
#ifdef DRIVELAB_TEST_FRESH_PROBE
#include "fakes/fresh_probe_api.h"
#else
#include "platform/linux/fresh_probe_api.h"
#endif

namespace drivelab {
namespace {
namespace api = fresh_probe_api;
using State = FreshSignatureState;
FreshSignatureEvidence failure(State state, std::string detail, int error = 0) {
    FreshSignatureEvidence e; e.state = state; e.detail = std::move(detail);
    if (error) e.native_error = error;
    return e;
}
FreshSignatureEvidence ioFailure(const std::string& detail, int error) {
    return failure(error == EACCES || error == EPERM ? State::PermissionDenied :
        error == ENOENT || error == ENODEV || error == ENXIO ? State::Disappeared : State::ReadFailure, detail, error);
}
struct ProbeDeleter { void operator()(std::remove_pointer_t<api::blkid_probe>* p) const noexcept { api::blkid_free_probe(p); } };
struct Descriptor {
    int fd;
    ~Descriptor() { if (fd >= 0) (void)api::close(fd); }
};
bool matches(const api::Status& s, const BlockDeviceNumber& number) {
    return api::is_block(s) && api::device_major(s.st_rdev) == number.major_number &&
           api::device_minor(s.st_rdev) == number.minor_number;
}
std::optional<std::uint64_t> sectors(const OwnershipRead<std::string>& input) {
    if (!input.value) return {};
    auto text = *input.value;
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    std::uint64_t value = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || r.ec != std::errc{} || r.ptr != text.data() + text.size()) return {};
    return value;
}
std::optional<FreshSignatureEvidence> checkSignatureRegions(int fd, std::uint64_t bytes) {
    // Confirm readable membership-label regions independently of libblkid:
    // library probing may treat short reads as "not found". These bounded
    // reads are not a surface test, and never interpret/modify device data.
    constexpr std::uint64_t region = 1024 * 1024;
    std::array<char, 64 * 1024> buffer{};
    const auto head = std::min(bytes, region);
    const auto tail = std::max(head, bytes > region ? bytes - region : 0);
    for (const auto& range : {std::pair<std::uint64_t,std::uint64_t>{0,head}, {tail,bytes}}) {
        for (auto offset = range.first; offset < range.second;) {
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),range.second-offset));
            std::ptrdiff_t count = -1; int retries = 0;
            do { errno = 0; count = api::read_at(fd,buffer.data(),length,offset); }
            while (count < 0 && errno == EINTR && ++retries < 3);
            if (count < 0) return ioFailure("Cannot read membership-label region",errno);
            if (static_cast<std::size_t>(count) != length)
                return failure(State::ReadFailure,"Short read in membership-label region",EIO);
            offset += length;
        }
    }
    return {};
}
FreshSignatureEvidence capture(int fd, const BlockDeviceObservation& node) {
    std::unique_ptr<std::remove_pointer_t<api::blkid_probe>, ProbeDeleter> probe(api::blkid_new_probe());
    if (!probe) return ioFailure("Cannot allocate fresh probe", errno);
    if (api::blkid_probe_set_device(probe.get(), fd, 0, 0) != 0 ||
        api::blkid_probe_enable_superblocks(probe.get(), 1) != 0 ||
        api::blkid_probe_set_superblocks_flags(probe.get(), api::signature_flags) != 0 ||
        api::blkid_probe_enable_partitions(probe.get(), 1) != 0 ||
        api::blkid_probe_enable_topology(probe.get(), 0) != 0)
        return ioFailure("Cannot configure complete fresh probe", errno);
    const auto bytes = api::blkid_probe_get_size(probe.get());
    if (bytes <= 0 || static_cast<std::uint64_t>(bytes) != *node.capacity_bytes)
        return failure(State::Disappeared, "Device capacity changed");
    errno = 0;
    const int result = api::blkid_do_safeprobe(probe.get());
    const int probe_error = errno;
    if (result == -2) return failure(State::Conflicting, "Ambivalent signatures", probe_error);
    if (result < 0 || probe_error != 0) return ioFailure("Fresh probing failed or reported a read error", probe_error);
    if (result != 0 && result != 1) return failure(State::Unsupported, "Unknown probe result");
    FreshSignatureEvidence e;
    bool malformed = false;
    auto tag = [&](const char* name) {
        const char* value = nullptr; std::size_t length = 0;
        if (api::blkid_probe_lookup_value(probe.get(), name, &value, &length) != 0) return std::string{};
        if (!value || !length || length > 256) { malformed = true; return std::string{}; }
        if (value[length - 1] == '\0') --length;
        std::string text(value, length);
        if (text.empty() || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c >= 127; }))
            malformed = true;
        return text;
    };
    e.type = tag("TYPE"); e.usage = tag("USAGE"); e.partition_table = tag("PTTYPE");
    if (malformed || e.type.empty() != e.usage.empty() ||
        (result == 1 && (!e.type.empty() || !e.partition_table.empty())))
        return failure(State::Conflicting, "Malformed or contradictory probe tags");
    if (!e.type.empty() && !e.partition_table.empty())
        return failure(State::Conflicting, "Overlapping filesystem/member and partition table");
    if (!e.partition_table.empty()) {
        if (node.node_type != BlockNodeType::Disk ||
            (e.partition_table != "gpt" && e.partition_table != "dos"))
            return failure(State::Unsupported, "Nested or unsupported partition table");
        errno = 0;
        const auto list = api::blkid_probe_get_partitions(probe.get());
        if (!list || errno) return ioFailure("Cannot read complete partition table", errno);
        const int count = api::blkid_partlist_numof_partitions(list);
        if (count < 0 || count > 4096) return failure(State::Unsupported, "Partition count outside bounds");
        std::set<int> numbers;
        for (int i = 0; i < count; ++i) {
            const auto p = api::blkid_partlist_get_partition(list, i);
            if (!p) return failure(State::Conflicting, "Missing partition entry");
            const int number = api::blkid_partition_get_partno(p);
            const auto start = api::blkid_partition_get_start(p), size = api::blkid_partition_get_size(p);
            if (number <= 0 || !numbers.insert(number).second || start < 0 || size <= 0 ||
                static_cast<std::uint64_t>(start) > *node.capacity_bytes / 512 ||
                static_cast<std::uint64_t>(size) > *node.capacity_bytes / 512 - static_cast<std::uint64_t>(start))
                return failure(State::Conflicting, "Invalid partition geometry");
            if (api::blkid_partition_is_extended(p) || api::blkid_partition_is_logical(p) || api::blkid_partition_get_flags(p))
                return failure(State::Unsupported, "Extended/logical or flagged partition requires further analysis");
            if (e.partition_table == "gpt") {
                const char* type = api::blkid_partition_get_type_string(p);
                std::string guid = type ? type : "";
                std::transform(guid.begin(), guid.end(), guid.begin(),
                    [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c); });
                if (guid != "0fc63daf-8483-4772-8e79-3d69d8477de4" &&
                    guid != "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7" &&
                    guid != "c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
                    return failure(State::Unsupported, "Partition type may declare membership or unsupported ownership");
            } else {
                const int type = api::blkid_partition_get_type(p);
                if (type != 0x83 && type != 0x07 && type != 0x0b && type != 0x0c && type != 0x06 && type != 0xef)
                    return failure(State::Unsupported, "Partition type may declare membership or unsupported ownership");
            }
            e.partitions.push_back({number, static_cast<std::uint64_t>(start), static_cast<std::uint64_t>(size)});
        }
        std::sort(e.partitions.begin(), e.partitions.end(), [](const auto& a, const auto& b) { return a.start_sector < b.start_sector; });
        std::uint64_t end = 0;
        for (const auto& p : e.partitions) {
            if (p.start_sector < end) return failure(State::Conflicting, "Overlapping partitions");
            end = p.start_sector + p.size_sectors;
        }
    }
    if (e.type.empty()) {
        if (result == 0 && e.partition_table.empty()) return failure(State::Unsupported, "Unclassified positive probe");
        e.state = State::None;
    } else if (e.type == "LVM2_member" || e.type == "linux_raid_member" || e.type == "zfs_member") {
        e.state = State::Membership;
    } else if (e.usage == "filesystem" &&
        (e.type == "ext2" || e.type == "ext3" || e.type == "ext4" || e.type == "vfat" || e.type == "exfat" || e.type == "ntfs")) {
        e.state = State::Filesystem;
    } else {
        e.state = State::Unsupported; e.detail = "Unsupported signature/crypto/multi-device filesystem: " + e.type;
    }
    return e;
}
FreshSignatureEvidence probeNode(const ResolvedBlockNode& b, const ResolvedInventorySnapshot& inventory, OwnershipIo& io) {
    const auto& n = b.observation;
    if (!b.current_path || !b.current_path->starts_with("/dev/") || !n.device_number ||
        !n.capacity_bytes || *n.capacity_bytes < 4096 ||
        *n.capacity_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return failure(State::Unsupported, "Missing/unsupported device locator or capacity");
    errno = 0;
    Descriptor fd{api::open(b.current_path->c_str(), api::read_flags)};
    if (fd.fd < 0) return ioFailure("Cannot open device read-only", errno);
    api::Status status{};
    if (api::fstat(fd.fd, &status) != 0) return ioFailure("Cannot inspect opened device", errno);
    if (!matches(status, *n.device_number)) return failure(State::Disappeared, "Opened device does not match inventory");
    if (const auto failed = checkSignatureRegions(fd.fd,*n.capacity_bytes)) return *failed;
    // Each capture allocates a new low-level probe and new buffers, bypassing
    // libblkid's persistent cache and the preceding capture's in-memory cache.
    auto e = capture(fd.fd, n);
    if (freshSignatureComplete(e)) {
        const auto again = capture(fd.fd, n);
        if (!freshSignatureComplete(again)) e = again;
        else if (again != e) e = failure(State::Conflicting, "Fresh signature/partition captures disagree");
    }
    if (freshSignatureComplete(e) && n.node_type == BlockNodeType::Disk) {
        std::vector<const ResolvedBlockNode*> children;
        for (const auto& child : inventory.nodes)
            if (child.observation.parent_kernel_name == n.kernel_name) children.push_back(&child);
        if (children.size() != e.partitions.size() || children.size() != n.child_kernel_names.size())
            e = failure(State::Conflicting, "Fresh partition table differs from kernel descendants");
        else for (const auto* child : children) {
            const auto part = std::find_if(e.partitions.begin(), e.partitions.end(), [&](const auto& p) {
                return child->observation.partition_number == static_cast<std::uint32_t>(p.number);
            });
            const auto path = "/sys/class/block/" + child->observation.kernel_name + "/start";
            const auto before = sectors(io.text(path)), after = sectors(io.text(path));
            if (part == e.partitions.end() || !before || before != after ||
                *before != part->start_sector || child->observation.capacity_bytes != part->size_sectors * 512) {
                e = failure(State::Conflicting, "Kernel partition extent differs from fresh table"); break;
            }
        }
    }
    if (freshSignatureComplete(e) && (b.signature.state == SignatureState::Conflicting ||
        b.signature.state == SignatureState::Malformed ||
        (b.signature.type && *b.signature.type != e.type) ||
        (b.signature.usage && *b.signature.usage != e.usage)))
        e = failure(State::Conflicting, "Fresh signature disagrees with retained cached evidence");
    if (freshSignatureComplete(e))
        if (const auto failed = checkSignatureRegions(fd.fd,*n.capacity_bytes)) e = *failed;
    api::Status path_status{}, final_status{};
    if (api::fstat(fd.fd, &final_status) != 0 || api::stat(b.current_path->c_str(), &path_status) != 0)
        e = ioFailure("Device disappeared or became unreadable after probe", errno);
    else if (!matches(path_status, *n.device_number) || !matches(final_status, *n.device_number))
        e = failure(State::Disappeared, "Device locator changed during probe");
    const int owned = fd.fd; fd.fd = -1;
    if (api::close(owned) != 0) e = ioFailure("Device close failed", errno);
    return e;
}
}
FreshSignatures NativeFreshSignatureSource::read(const ResolvedInventorySnapshot& inventory) {
    FreshSignatures result;
    if (inventory.nodes.size() > 4096) {
        result[""] = failure(State::Unsupported, "Fresh probe inventory limit"); return result;
    }
    const auto before = raw_.scan();
    const auto first = before ? std::optional(resolveBlockIdentities(before.value())) : std::nullopt;
    auto matchesSnapshot = [&](const auto& snapshot, const ResolvedBlockNode& expected) {
        if (!snapshot || snapshot->nodes.size() != inventory.nodes.size()) return false;
        if (std::any_of(snapshot->inventory_issues.begin(),snapshot->inventory_issues.end(),[&](const auto& issue) {
            return !issue.kernel_name || *issue.kernel_name == expected.observation.kernel_name ||
                issue.code == BlockInventoryIssueCode::InvalidRelationship ||
                issue.code == BlockInventoryIssueCode::DuplicateRecord ||
                issue.code == BlockInventoryIssueCode::DeviceDisappeared;
        })) return false;
        const auto it = std::find_if(snapshot->nodes.begin(), snapshot->nodes.end(), [&](const auto& b) {
            return b.observation.kernel_name == expected.observation.kernel_name;
        });
        return it != snapshot->nodes.end() && it->observation == expected.observation && it->identity == expected.identity;
    };
    const bool supported = api::blkid_known_fstype("LVM2_member") == 1 &&
        api::blkid_known_fstype("linux_raid_member") == 1 && api::blkid_known_fstype("zfs_member") == 1;
    auto diskCandidate = [&](const ResolvedBlockNode& b) {
        if (b.observation.node_type == BlockNodeType::Disk)
            return b.observation.physical_kind == PhysicalDeviceKind::Disk;
        if (b.observation.node_type != BlockNodeType::Partition || !b.observation.parent_kernel_name)
            return false;
        const ResolvedBlockNode* parent = nullptr;
        for (const auto& candidate : inventory.nodes) {
            if (candidate.observation.kernel_name != *b.observation.parent_kernel_name) continue;
            if (parent) return false;
            parent = &candidate;
        }
        return parent && parent->observation.node_type == BlockNodeType::Disk &&
            parent->observation.physical_kind == PhysicalDeviceKind::Disk &&
            parent->observation.observation_kind != BlockObservationKind::VirtualOrPseudo;
    };
    for (const auto& b : inventory.nodes) {
        if (b.observation.observation_kind == BlockObservationKind::VirtualOrPseudo) continue;
        auto& e = result[blockOwnershipKey(b.observation.kernel_name)];
        if (scope_ == PhysicalAssessmentScope::Disks && !diskCandidate(b))
            e = failure(State::Unsupported, "Device kind is outside the disk safety workflow");
        else if (!first) e = failure(State::ReadFailure, "Cannot revalidate raw inventory before probing");
        else if (!matchesSnapshot(first, b)) e = failure(State::Disappeared, "Raw identity changed before probing");
        else if (!supported) e = failure(State::Unsupported, "Required LVM/md/ZFS signature decoders unavailable");
        else if (b.observation.node_type == BlockNodeType::Disk && b.identity.state != ResolutionState::Resolved)
            e = failure(State::Unsupported, "Physical identity is unresolved");
        else e = probeNode(b, inventory, io_);
    }
    const auto after = raw_.scan();
    const auto final = after ? std::optional(resolveBlockIdentities(after.value())) : std::nullopt;
    for (const auto& b : inventory.nodes) {
        const auto key = blockOwnershipKey(b.observation.kernel_name);
        if (!result.contains(key)) continue;
        if (!final) result[key] = failure(State::ReadFailure, "Cannot revalidate raw inventory after probing");
        else if (!matchesSnapshot(final, b))
            result[key] = failure(State::Disappeared, "Raw inventory/physical identity changed after probing");
    }
    return result;
}
} // namespace drivelab
