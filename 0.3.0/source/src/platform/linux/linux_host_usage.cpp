#include "platform/linux/linux_host_usage.h"
#include "core/scan_profile.h"

#include <algorithm>
#include <map>

namespace drivelab {
namespace {

void addIssue(ResolvedInventorySnapshot& out, DiscoveryIssueCode code,
              const std::string& node, const std::string& field, const std::string& message) {
    out.issues.push_back({code, node, field, message});
}

bool matches(const HostRead<HostPathStatus>& status, const BlockDeviceObservation& node) {
    return status.value && status.issues.empty() && status.value->is_block &&
           node.device_number && status.value->device_number == node.device_number;
}

std::optional<std::size_t> correlate(ResolvedInventorySnapshot& out,
                                     const BlockDeviceNumber& number) {
    std::optional<std::size_t> match;
    for (std::size_t i = 0; i < out.nodes.size(); ++i) {
        if (out.nodes[i].observation.device_number != number) continue;
        if (match) return std::nullopt;
        match = i;
    }
    return match;
}

bool relationshipUsable(const BlockDeviceObservation& node) {
    return std::none_of(node.issues.begin(), node.issues.end(), [](const auto& issue) {
        return issue.code == BlockInventoryIssueCode::InvalidRelationship ||
               issue.code == BlockInventoryIssueCode::DeviceDisappeared ||
               issue.field == "parent_kernel_name" || issue.field == "partition_number";
    });
}

}  // namespace

LinuxResolvedInventoryProvider::LinuxResolvedInventoryProvider(
    BlockInventoryProvider& inventory, LinuxHostFiles& files, LinuxSignatureSource& signatures)
    : inventory_(inventory), files_(files), signatures_(signatures) {}

Result<ResolvedInventorySnapshot> LinuxResolvedInventoryProvider::scan() {
    ScanStage timing("inventory.resolved");
    auto raw = inventory_.scan();
    if (!raw) return Result<ResolvedInventorySnapshot>::failure(raw.error());
    auto out = [&] {
        ScanStage identity("inventory.identity");
        return resolveBlockIdentities(raw.value());
    }();
    ScanStage host_usage("inventory.direct_usage");
    auto mounts = files_.read(HostTextFile::MountInfo);
    auto swaps = files_.read(HostTextFile::Swaps);
    out.issues.insert(out.issues.end(), mounts.issues.begin(), mounts.issues.end());
    out.issues.insert(out.issues.end(), swaps.issues.begin(), swaps.issues.end());
    out.mountinfo_read_complete = mounts.value.has_value() && mounts.issues.empty();
    out.swaps_read_complete = swaps.value.has_value() && swaps.issues.empty();
    if (mounts.value) parseMountInfo(*mounts.value, out);
    else addIssue(out, DiscoveryIssueCode::ReadFailure, "", "mountinfo", "Mount namespace evidence unavailable");
    if (swaps.value) parseSwaps(*swaps.value, out);
    else addIssue(out, DiscoveryIssueCode::ReadFailure, "", "swaps", "Active swap evidence unavailable");
    for (const auto& problem : out.issues) {
        if (problem.field == "mountinfo") out.mountinfo_read_complete = false;
        if (problem.field == "swaps") out.swaps_read_complete = false;
    }

    std::map<std::uint64_t, std::size_t> mount_counts;
    std::map<std::string, std::size_t> swap_counts;
    for (const auto& mount : out.mounts) ++mount_counts[mount.mount_id];
    for (const auto& swap : out.swaps) ++swap_counts[swap.path];

    // Confirm current locators through metadata only, before and after acquisition.
    for (auto& node : out.nodes) {
        if (node.current_path) {
            const auto status = files_.inspect(*node.current_path);
            node.issues.insert(node.issues.end(), status.issues.begin(), status.issues.end());
            if (!matches(status, node.observation)) {
                node.current_path.reset();
                node.issues.push_back({DiscoveryIssueCode::SnapshotChanged,
                    node.observation.kernel_name, "current_path",
                    "Current path no longer names the observed block device number"});
            }
        }
        const auto source = signatures_.read(node.observation);
        node.signature = parseSignature(source, node.observation.kernel_name, node.issues);
        if (std::any_of(source.issues.begin(), source.issues.end(), [](const auto& issue) {
                return issue.code == DiscoveryIssueCode::Disappeared ||
                       issue.code == DiscoveryIssueCode::SnapshotChanged;
            })) node.current_path.reset();
        node.usage_nodes.push_back(node.observation.kernel_name);
    }

    for (std::size_t i = 0; i < out.mounts.size(); ++i) {
        auto& mount = out.mounts[i];
        auto index = correlate(out, mount.device_number);
        if (!index || mount_counts[mount.mount_id] != 1) {
            addIssue(out, DiscoveryIssueCode::UncorrelatedUsage, "", "mountinfo",
                     "Mount " + std::to_string(mount.mount_id) +
                     " has no unique directly observed block node; source=" + mount.source);
            out.issues.back().mount_id = mount.mount_id;
            continue;
        }
        auto& node = out.nodes[*index];
        // The mountinfo device number is authoritative direct evidence. A /dev
        // spelling that now names something else is retained as a separate issue.
        mount.node = node.observation.kernel_name;
        node.mounts.push_back(i);
        if (mount.source.starts_with("/dev/")) {
            auto status = files_.inspect(mount.source);
            out.issues.insert(out.issues.end(), status.issues.begin(), status.issues.end());
            if (!matches(status, node.observation)) {
                addIssue(out, DiscoveryIssueCode::SnapshotChanged, *mount.node, "mount_source",
                         "Mount source path cannot corroborate mountinfo major/minor: " + mount.source);
            }
        }
        if (mount.subtree) {
            addIssue(out, DiscoveryIssueCode::UnsupportedTopology, *mount.node, "mount_root",
                     "Non-root mount subtree retained; bind mount versus subvolume is not inferred: " + mount.root);
        }
        if (node.signature.state == SignatureState::Reported &&
            (node.signature.usage != "filesystem" ||
             node.signature.type != mount.filesystem_type)) {
            node.signature.state = SignatureState::Conflicting;
            node.issues.push_back({DiscoveryIssueCode::Conflict, *mount.node, "signature",
                "Cached signature disagrees with mountinfo filesystem; both observations retained"});
        }
    }
    for (std::size_t i = 0; i < out.swaps.size(); ++i) {
        auto& swap = out.swaps[i];
        if (swap.type != "partition" || swap_counts[swap.path] != 1) {
            addIssue(out, DiscoveryIssueCode::UnsupportedTopology, "", "swap_source",
                     "Swap file, unsupported type, or duplicate path retained without physical attribution: " + swap.path);
            continue;
        }
        auto first = files_.inspect(swap.path);
        auto second = files_.inspect(swap.path);
        out.issues.insert(out.issues.end(), first.issues.begin(), first.issues.end());
        out.issues.insert(out.issues.end(), second.issues.begin(), second.issues.end());
        if (!first.value || !second.value || !first.issues.empty() || !second.issues.empty() ||
            *first.value != *second.value || !first.value->is_block || !first.value->device_number) {
            addIssue(out, DiscoveryIssueCode::UncorrelatedUsage, "", "swap_source",
                     "Swap path is unreadable, changed, or is not a block node: " + swap.path);
            continue;
        }
        swap.device_number = first.value->device_number;
        auto index = correlate(out, *swap.device_number);
        if (!index) {
            addIssue(out, DiscoveryIssueCode::UncorrelatedUsage, "", "swap_source",
                     "No unique raw node for active swap: " + swap.path);
            continue;
        }
        auto& node = out.nodes[*index];
        swap.node = node.observation.kernel_name;
        node.swaps.push_back(i);
        if (node.signature.state == SignatureState::Reported && node.signature.type != "swap") {
            node.signature.state = SignatureState::Conflicting;
            node.issues.push_back({DiscoveryIssueCode::Conflict, *swap.node, "signature",
                                  "Cached signature disagrees with active swap; observations retained"});
        }
    }

    for (auto& node : out.nodes) {
        if (node.current_path) {
            const auto status = files_.inspect(*node.current_path);
            node.issues.insert(node.issues.end(), status.issues.begin(), status.issues.end());
            if (!matches(status, node.observation)) {
                node.current_path.reset();
                node.issues.push_back({DiscoveryIssueCode::SnapshotChanged,
                    node.observation.kernel_name, "current_path",
                    "Device path changed or disappeared during host usage acquisition"});
            }
        }
    }

    // Only the accepted, reciprocal, unique disk/partition edge is traversed.
    // No holder/slave, dm, LVM, RAID, ZFS, or host ownership traversal exists here.
    for (const auto& child : out.nodes) {
        const auto& observed = child.observation;
        if (observed.node_type != BlockNodeType::Partition) continue;
        ResolvedBlockNode* parent = nullptr;
        std::size_t parents = 0;
        if (observed.parent_kernel_name) {
            for (auto& candidate : out.nodes) {
                if (candidate.observation.kernel_name == *observed.parent_kernel_name) {
                    parent = &candidate; ++parents;
                }
            }
        }
        if (parents != 1 || !parent || !relationshipUsable(observed) ||
            !relationshipUsable(parent->observation) || !observed.partition_number ||
            parent->observation.node_type != BlockNodeType::Disk ||
            parent->identity.state != ResolutionState::Resolved ||
            parent->observation.observation_kind == BlockObservationKind::VirtualOrPseudo ||
            std::count(parent->observation.child_kernel_names.begin(),
                       parent->observation.child_kernel_names.end(), observed.kernel_name) != 1) {
            addIssue(out, DiscoveryIssueCode::UnsupportedTopology, observed.kernel_name, "usage_parent",
                     "No unambiguous resolved physical/namespace parent for this partition");
            continue;
        }
        parent->usage_nodes.push_back(observed.kernel_name);
        parent->mounts.insert(parent->mounts.end(), child.mounts.begin(), child.mounts.end());
        parent->swaps.insert(parent->swaps.end(), child.swaps.begin(), child.swaps.end());
    }
    for (auto& node : out.nodes) {
        if ((!node.mounts.empty() || !node.swaps.empty()) &&
            node.observation.observation_kind == BlockObservationKind::VirtualOrPseudo) {
            node.issues.push_back({DiscoveryIssueCode::UnsupportedTopology,
                node.observation.kernel_name, "usage_parent",
                "Direct usage observed on virtual node; underlying physical ownership deferred to 0.3.0C"});
        }
        std::sort(node.mounts.begin(), node.mounts.end());
        std::sort(node.swaps.begin(), node.swaps.end());
        std::sort(node.usage_nodes.begin(), node.usage_nodes.end());
        sortDiscoveryIssues(node.issues);
    }
    sortDiscoveryIssues(out.issues);
    return Result<ResolvedInventorySnapshot>::success(std::move(out));
}

Result<std::string> LinuxResolvedInventoryProvider::resolvePath(const DriveId& expected) {
    auto fresh = scan();
    if (!fresh) return Result<std::string>::failure(fresh.error());
    return resolveCurrentPath(fresh.value(), expected);
}

}  // namespace drivelab
