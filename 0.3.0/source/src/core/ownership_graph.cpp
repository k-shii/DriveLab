#include "core/ownership_graph.h"
#include "core/scan_control.h"
#include "core/scan_profile.h"

#include <algorithm>
#include <functional>
#include <set>
#include <tuple>
#include <unordered_set>

namespace drivelab {
namespace {
using Code = OwnershipReasonCode;

void normalize(std::vector<OwnershipReason>& reasons) {
    std::sort(reasons.begin(), reasons.end(), [](const auto& a, const auto& b) {
        return std::tie(a.code, a.node, a.source, a.detail) < std::tie(b.code, b.node, b.source, b.detail);
    });
    reasons.erase(std::unique(reasons.begin(), reasons.end()), reasons.end());
}
OwnershipReason claimReason(const OwnershipClaim& c) {
    Code code = Code::Holder;
    switch (c.use) {
        case OwnershipUse::Held: code = Code::Holder; break;
        case OwnershipUse::Mounted: code = Code::Mount; break;
        case OwnershipUse::Root: code = Code::SystemRoot; break;
        case OwnershipUse::Boot: code = Code::BootFilesystem; break;
        case OwnershipUse::Home: code = Code::HomeFilesystem; break;
        case OwnershipUse::Swap: code = Code::ActiveSwap; break;
        case OwnershipUse::Lvm: code = Code::LvmMember; break;
        case OwnershipUse::Raid: code = Code::RaidMember; break;
        case OwnershipUse::Zfs: code = Code::ZfsMember; break;
        case OwnershipUse::ConfiguredStorage: code = Code::ConfiguredStorage; break;
        case OwnershipUse::Guest: code = Code::GuestAssignment; break;
        case OwnershipUse::OpenBlockHandle: code = Code::OpenBlockHandle; break;
        case OwnershipUse::OpenFileHandle: code = Code::OpenFileHandle; break;
    }
    return {code, c.consumer, c.source, c.detail};
}
bool protects(OwnershipUse use) {
    return use != OwnershipUse::Held && use != OwnershipUse::OpenBlockHandle && use != OwnershipUse::OpenFileHandle;
}
} // namespace
bool pmxcfsOwnershipMount(const MountEvidence& m) {
    return m.device_number.major_number == 0 && m.filesystem_type == "fuse" &&
           m.source == "/dev/fuse" && m.mount_point == "/etc/pve" && m.root == "/";
}
bool harmlessOwnershipMount(const MountEvidence& m) {
    static const std::set<std::string> kernel_filesystems{
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "ramfs", "cgroup", "cgroup2",
        "securityfs", "debugfs", "tracefs", "configfs", "pstore", "efivarfs", "hugetlbfs",
        "mqueue", "bpf", "fusectl", "binfmt_misc", "rpc_pipefs", "autofs"
    };
    // These identify pseudo interfaces, not absence of their daemon's file use.
    // pmxcfs additionally requires the native process source's backing check.
    return pmxcfsOwnershipMount(m) || (m.device_number.major_number == 0 &&
        ((m.filesystem_type == "fuse.lxcfs" && m.source == "lxcfs") ||
         (!m.source.starts_with("/dev/") && kernel_filesystems.contains(m.filesystem_type))));
}
namespace {
bool harmlessMountIssue(const DiscoveryIssue& issue, const ResolvedInventorySnapshot& inventory) {
    if (issue.code != DiscoveryIssueCode::UncorrelatedUsage || issue.field != "mountinfo" ||
        !issue.node.empty() || issue.native_error || !issue.mount_id) return false;
    const MountEvidence* found = nullptr;
    for (const auto& mount : inventory.mounts) {
        if (mount.mount_id != issue.mount_id) continue;
        if (found) return false; // Duplicate mount ID is ambiguity, even for pseudo filesystems.
        found = &mount;
    }
    return found && !found->node && harmlessOwnershipMount(*found) &&
        std::none_of(inventory.nodes.begin(), inventory.nodes.end(), [&](const auto& b) {
            return b.observation.device_number == found->device_number;
        });
}
}  // namespace

bool freshSignatureComplete(const FreshSignatureEvidence& e) {
    return e.state == FreshSignatureState::None || e.state == FreshSignatureState::Filesystem ||
           e.state == FreshSignatureState::Membership;
}
std::string freshSignatureStateName(FreshSignatureState state) {
    switch (state) {
        case FreshSignatureState::Unavailable: return "Unavailable";
        case FreshSignatureState::None: return "None";
        case FreshSignatureState::Filesystem: return "Filesystem";
        case FreshSignatureState::Membership: return "Membership";
        case FreshSignatureState::Unsupported: return "Unsupported";
        case FreshSignatureState::PermissionDenied: return "PermissionDenied";
        case FreshSignatureState::ReadFailure: return "ReadFailure";
        case FreshSignatureState::Disappeared: return "Disappeared";
        case FreshSignatureState::Conflicting: return "Conflicting";
    }
    return "Unrecognized";
}

std::string blockOwnershipKey(const std::string& name) { return "block:" + name; }

std::string ownershipReasonName(OwnershipReasonCode code) {
    switch (code) {
        case Code::ChecksComplete: return "ChecksComplete";
        case Code::IdentityUnavailable: return "IdentityUnavailable";
        case Code::PathUnavailable: return "PathUnavailable";
        case Code::ReadOnly: return "ReadOnly";
        case Code::Mount: return "Mount";
        case Code::SystemRoot: return "SystemRoot";
        case Code::BootFilesystem: return "BootFilesystem";
        case Code::HomeFilesystem: return "HomeFilesystem";
        case Code::ActiveSwap: return "ActiveSwap";
        case Code::Holder: return "Holder";
        case Code::LvmMember: return "LvmMember";
        case Code::RaidMember: return "RaidMember";
        case Code::ZfsMember: return "ZfsMember";
        case Code::ConfiguredStorage: return "ConfiguredStorage";
        case Code::GuestAssignment: return "GuestAssignment";
        case Code::OpenBlockHandle: return "OpenBlockHandle";
        case Code::OpenFileHandle: return "OpenFileHandle";
        case Code::MissingSource: return "MissingSource";
        case Code::MissingRelationship: return "MissingRelationship";
        case Code::AsymmetricRelationship: return "AsymmetricRelationship";
        case Code::DuplicateNode: return "DuplicateNode";
        case Code::Cycle: return "Cycle";
        case Code::UnsupportedTopology: return "UnsupportedTopology";
        case Code::ConflictingEvidence: return "ConflictingEvidence";
        case Code::SnapshotChanged: return "SnapshotChanged";
        case Code::IncompleteCoverage: return "IncompleteCoverage";
        case Code::SignatureUnavailable: return "SignatureUnavailable";
        case Code::NamespaceScope: return "NamespaceScope";
    }
    return "Unrecognized";
}

OwnershipAssessment assessOwnership(const ResolvedInventorySnapshot& inventory,
                                    OwnershipEvidence evidence, PhysicalAssessmentScope scope) {
    OwnershipAssessment out;
    out.coverage = evidence.coverage;
    out.coverage_gaps = std::move(evidence.coverage_gaps);
    for (auto& [source, gaps] : out.coverage_gaps) { (void)source; normalize(gaps); }
    out.fresh_signatures = evidence.fresh_signatures;
    out.namespace_mounts = evidence.namespace_mounts;
    std::sort(out.namespace_mounts.begin(), out.namespace_mounts.end(), [](const auto& a, const auto& b) {
        const auto& x = a.mount; const auto& y = b.mount;
        return std::tie(a.namespace_id, x.mount_id, x.parent_id, x.device_number.major_number,
            x.device_number.minor_number, x.root, x.mount_point, x.options, x.optional_fields,
            x.filesystem_type, x.source, x.super_options, x.node, x.subtree) <
            std::tie(b.namespace_id, y.mount_id, y.parent_id, y.device_number.major_number,
            y.device_number.minor_number, y.root, y.mount_point, y.options, y.optional_fields,
            y.filesystem_type, y.source, y.super_options, y.node, y.subtree);
    });
    out.namespace_mounts.erase(std::unique(out.namespace_mounts.begin(), out.namespace_mounts.end()), out.namespace_mounts.end());
    out.coverage["mountinfo"] = inventory.mountinfo_read_complete;
    out.coverage["swaps"] = inventory.swaps_read_complete;
    for (const char* name : {"sysfs", "zfs", "proxmox", "processes", "inactive_membership"}) {
        if (!out.coverage.contains(name)) out.coverage[name] = false;
    }
    out.issues = std::move(evidence.issues);
    std::map<std::string, const ResolvedBlockNode*> blocks;
    std::set<std::string> duplicates;
    for (const auto& b : inventory.nodes) {
        const auto key = blockOwnershipKey(b.observation.kernel_name);
        if (!blocks.emplace(key, &b).second) duplicates.insert(key);
        evidence.nodes.push_back({key, OwnershipKind::Block, {{"kernel_name", b.observation.kernel_name}}, true});
    }
    std::sort(evidence.nodes.begin(), evidence.nodes.end(), [](const auto& a, const auto& b) {
        return std::tie(a.key, a.kind, a.facts, a.complete) < std::tie(b.key, b.kind, b.facts, b.complete);
    });
    std::map<std::string, OwnershipNode> nodes;
    for (const auto& n : evidence.nodes) {
        auto [it, inserted] = nodes.emplace(n.key, n);
        if (!inserted) duplicates.insert(n.key);
    }
    for (const auto& key : duplicates) {
        nodes[key].complete = false;
        out.issues.push_back({Code::DuplicateNode, key, "graph", "Duplicate graph key; no record selected as authoritative"});
    }
    // A supplies reciprocal partition links. They remain observations, not IDs.
    for (const auto& [key, b] : blocks) {
        const auto& raw = b->observation;
        if (raw.node_type != BlockNodeType::Partition) continue;
        const auto parent_key = blockOwnershipKey(raw.parent_kernel_name.value_or(""));
        const auto parent = blocks.find(parent_key);
        const bool clean = std::none_of(raw.issues.begin(), raw.issues.end(), [](const auto& p) {
            return p.code == BlockInventoryIssueCode::InvalidRelationship ||
                   p.code == BlockInventoryIssueCode::DeviceDisappeared;
        });
        if (!clean || !raw.partition_number || parent == blocks.end() ||
            parent->second->observation.node_type != BlockNodeType::Disk ||
            std::count(parent->second->observation.child_kernel_names.begin(),
                       parent->second->observation.child_kernel_names.end(), raw.kernel_name) != 1) {
            nodes[key].complete = false;
            out.issues.push_back({Code::MissingRelationship, key, "inventory", "Partition parent is not reciprocal and unambiguous"});
        } else evidence.edges.push_back({key, parent_key, "partition-of", "0.3.0A"});
    }
    std::sort(evidence.edges.begin(), evidence.edges.end(), [](const auto& a, const auto& b) {
        return std::tie(a.consumer, a.provider, a.relation, a.source) <
               std::tie(b.consumer, b.provider, b.relation, b.source);
    });
    evidence.edges.erase(std::unique(evidence.edges.begin(), evidence.edges.end()), evidence.edges.end());
    out.edges = evidence.edges;
    std::map<std::string, std::set<std::string>> parents;
    for (const auto& edge : out.edges) {
        if (!nodes.contains(edge.consumer) || !nodes.contains(edge.provider)) {
            if (nodes.contains(edge.consumer)) nodes[edge.consumer].complete = false;
            if (nodes.contains(edge.provider)) nodes[edge.provider].complete = false;
            out.issues.push_back({Code::MissingRelationship, edge.consumer, edge.source, "Missing graph endpoint: " + edge.provider});
        }
        parents[edge.consumer].insert(edge.provider);
    }
    for (const auto& issue : out.issues) {
        if (nodes.contains(issue.node)) nodes[issue.node].complete = false;
    }
    std::map<std::string, PhysicalOwner> physical;
    for (const auto& [key, b] : blocks) {
        if (b->observation.node_type == BlockNodeType::Disk &&
            b->observation.observation_kind != BlockObservationKind::VirtualOrPseudo) {
            physical[key] = {key, b->identity.id, b->identity.scope};
            if (!parents[key].empty()) {
                nodes[key].complete = false;
                out.issues.push_back({Code::ConflictingEvidence, key, "graph", "Physical endpoint unexpectedly has backing edges"});
            }
        }
    }

    // Track structural completeness separately from endpoint identity/path failures.
    std::map<std::string, bool> topology_complete;
    // Trace each node separately so cycle results do not depend on traversal order.
    for (const auto& [key, node] : nodes) {
        (void)node;
        OwnershipTrace trace;
        trace.consumer = key;
        trace.complete = true;
        bool topology_ok = true;
        std::set<std::string> active, visited, owners;
        std::function<void(const std::string&, std::size_t)> walk = [&](const std::string& current, std::size_t depth) {
            if (depth > 256 || active.contains(current)) {
                trace.complete = false;
                topology_ok = false;
                trace.reasons.push_back({Code::Cycle, current, "graph", "Ownership cycle or depth limit"});
                return;
            }
            if (visited.contains(current)) return;
            const auto it = nodes.find(current);
            if (it == nodes.end() || !it->second.complete) {
                trace.complete = false;
                topology_ok = false;
                trace.reasons.push_back({Code::MissingRelationship, current, "graph", "Incomplete or missing ownership node"});
                for (const auto& reason : out.issues)
                    if (reason.node == current) trace.reasons.push_back(reason);
            }
            if (const auto owner = physical.find(current); owner != physical.end()) {
                owners.insert(current);
                const auto* b = blocks.at(current);
                if (b->identity.state != ResolutionState::Resolved || !owner->second.id) {
                    trace.complete = false;
                    trace.reasons.push_back({Code::IdentityUnavailable, current, "identity", "Physical endpoint has no unambiguous stable ID"});
                }
                if (!b->current_path) {
                    trace.complete = false;
                    trace.reasons.push_back({Code::PathUnavailable, current, "identity", "No verified current path"});
                }
            } else if (parents[current].empty()) {
                trace.complete = false;
                topology_ok = false;
                trace.reasons.push_back({Code::MissingRelationship, current, "graph", "No physical endpoint or backing relationship"});
            } else {
                active.insert(current);
                for (const auto& parent : parents[current]) walk(parent, depth + 1);
                active.erase(current);
            }
            visited.insert(current);
        };
        walk(key, 0);
        for (const auto& owner : owners) trace.owners.push_back(physical.at(owner));
        normalize(trace.reasons);
        topology_complete[key] = topology_ok;
        out.traces.push_back(std::move(trace));
    }
    std::map<std::string, const OwnershipTrace*> traces;
    for (const auto& t : out.traces) traces[t.consumer] = &t;
    // A shared consumer retains its aggregate reasons. A physical owner's
    // decision excludes only failures proven to belong to a separate backing
    // branch. Unattributed endpoints, cycles and structural gaps remain shared.
    auto ownerReasons = [&](const OwnershipTrace& trace, const std::string& owner) {
        std::vector<OwnershipReason> result;
        for (const auto& reason : trace.reasons) {
            const auto branch = traces.find(reason.node);
            if (branch != traces.end() && topology_complete.at(reason.node) &&
                !branch->second->owners.empty() &&
                std::none_of(branch->second->owners.begin(), branch->second->owners.end(),
                    [&](const auto& o) { return o.node == owner; })) continue;
            result.push_back(reason);
        }
        return result;
    };
    const std::set<std::string> inactive_loops(evidence.inactive_loops.begin(), evidence.inactive_loops.end());
    bool unknown_usage = std::any_of(out.traces.begin(), out.traces.end(),
        [&](const auto& t) { return !topology_complete.at(t.consumer) && !inactive_loops.contains(t.consumer); });
    for (const auto& [key, fresh] : out.fresh_signatures) {
        if (fresh.state != FreshSignatureState::Membership || !blocks.contains(key)) continue;
        const auto use = fresh.type == "LVM2_member" ? OwnershipUse::Lvm :
                         fresh.type == "zfs_member" ? OwnershipUse::Zfs : OwnershipUse::Raid;
        evidence.claims.push_back({key, use, "libblkid:fresh", "Fresh inactive/active membership: " + fresh.type});
    }
    std::unordered_set<std::string> claim_sources;
    {
        ScanStage index("classification.mount_claim_index");
        for (const auto& claim : evidence.claims) claim_sources.insert(claim.source);
    }
    for (const auto& mount : inventory.mounts) {
        checkScanCancelled();
        if (!mount.node) {
            // ZFS mount claims can be supplied by the pool source.
            const bool covered = [&] {
                ScanStage lookup("classification.mount_claim_lookup");
                return claim_sources.contains("mountinfo:" + std::to_string(mount.mount_id));
            }();
            if (!covered && !harmlessOwnershipMount(mount)) unknown_usage = true;
            continue;
        }
        const auto use = mount.mount_point == "/" ? OwnershipUse::Root :
            (mount.mount_point == "/boot" || mount.mount_point.starts_with("/boot/")) ? OwnershipUse::Boot :
            (mount.mount_point == "/home" || mount.mount_point.starts_with("/home/")) ? OwnershipUse::Home : OwnershipUse::Mounted;
        const auto source = "mountinfo:" + std::to_string(mount.mount_id);
        evidence.claims.push_back({blockOwnershipKey(*mount.node), use, source, mount.mount_point});
        claim_sources.insert(source); // Preserve coverage by preceding rows, even duplicate IDs.
    }
    for (const auto& swap : inventory.swaps) {
        if (!swap.node) { unknown_usage = true; continue; }
        evidence.claims.push_back({blockOwnershipKey(*swap.node), OwnershipUse::Swap, "swaps", swap.path});
    }
    std::sort(evidence.claims.begin(), evidence.claims.end(), [](const auto& a, const auto& b) {
        return std::tie(a.consumer, a.use, a.source, a.detail) < std::tie(b.consumer, b.use, b.source, b.detail);
    });
    evidence.claims.erase(std::unique(evidence.claims.begin(), evidence.claims.end()), evidence.claims.end());
    out.claims = evidence.claims;
    std::map<std::string, std::vector<OwnershipReason>> reasons;
    std::map<std::string, bool> incomplete, protected_use, busy_use;
    for (const auto& trace : out.traces) {
        for (const auto& owner : trace.owners) {
            if (!trace.complete) {
                const auto relevant = ownerReasons(trace, owner.node);
                if (!relevant.empty()) incomplete[owner.node] = true;
                reasons[owner.node].insert(reasons[owner.node].end(), relevant.begin(), relevant.end());
            }
        }
    }
    for (const auto& claim : out.claims) {
        checkScanCancelled();
        const auto found = traces.find(claim.consumer);
        if (found == traces.end() || !topology_complete.at(claim.consumer)) unknown_usage = true;
        if (found == traces.end()) continue;
        for (const auto& owner : found->second->owners) {
            reasons[owner.node].push_back(claimReason(claim));
            if (protects(claim.use)) protected_use[owner.node] = true;
            else busy_use[owner.node] = true;
            if (!ownerReasons(*found->second, owner.node).empty()) incomplete[owner.node] = true;
        }
    }
    const bool host_coverage = std::all_of(out.coverage.begin(), out.coverage.end(),
        [&](const auto& entry) {
            // A false summary with missing/empty explanations is still global.
            const auto gaps = out.coverage_gaps.find(entry.first);
            return entry.first == "inactive_membership" || entry.second ||
                (gaps != out.coverage_gaps.end() && !gaps->second.empty());
        });
    auto separateOwner = [&](const std::string& node, const std::string& owner) {
        const auto it = traces.find(node);
        return it != traces.end() && topology_complete.at(node) && !it->second->owners.empty() &&
            std::none_of(it->second->owners.begin(), it->second->owners.end(),
                [&](const auto& o) { return o.node == owner; });
    };
    auto supersededSignatureGap = [&](const DiscoveryIssue& issue) {
        const auto key = blockOwnershipKey(issue.node);
        return issue.field == "signature" && issue.code == DiscoveryIssueCode::MissingEvidence &&
            out.fresh_signatures.contains(key) && freshSignatureComplete(out.fresh_signatures.at(key));
    };
    for (const auto& [key, owner] : physical) {
        checkScanCancelled();
        const auto* b = blocks.at(key);
        if (scope == PhysicalAssessmentScope::Disks &&
            b->observation.physical_kind != PhysicalDeviceKind::Disk) continue;
        ClassifiedStorage decision{key, owner.id, DriveStatus::Unknown, reasons[key]};
        const auto* trace = traces.at(key);
        bool scoped_coverage_complete = true;
        for (const auto& [source, gaps] : out.coverage_gaps) {
            (void)source;
            for (const auto& gap : gaps) if (!separateOwner(gap.node,key)) {
                scoped_coverage_complete = false;
                decision.reasons.push_back(gap);
            }
        }
        bool local_unknown = incomplete[key] || !trace->complete;
        for (const auto& [node, fresh] : out.fresh_signatures) {
            if (fresh.state != FreshSignatureState::Disappeared && fresh.state != FreshSignatureState::Conflicting) continue;
            if (!traces.contains(node)) continue;
            if (std::any_of(traces.at(node)->owners.begin(), traces.at(node)->owners.end(),
                [&](const auto& o) { return o.node == key; })) {
                local_unknown = true;
                decision.reasons.push_back({Code::ConflictingEvidence, node, "libblkid:fresh", fresh.detail});
            }
        }
        if (local_unknown) {
            decision.reasons.insert(decision.reasons.end(), trace->reasons.begin(), trace->reasons.end());
        }
        if (local_unknown) decision.status = DriveStatus::Unknown;
        else if (protected_use[key] || b->observation.read_only == true) {
            decision.status = DriveStatus::Protected;
            if (b->observation.read_only == true)
                decision.reasons.push_back({Code::ReadOnly, key, "sysfs", "Device is read-only"});
        } else if (busy_use[key]) decision.status = DriveStatus::Busy;
        else {
            // Fresh negative evidence is required for this owner and every descendant.
            bool coverage_complete = host_coverage && scoped_coverage_complete;
            for (const auto& issue : out.issues)
                if (issue.source != "inactive_membership" && !separateOwner(issue.node, key))
                    coverage_complete = false;
            for (const auto& issue : inventory.inventory_issues)
                if (!issue.kernel_name || !separateOwner(blockOwnershipKey(*issue.kernel_name), key))
                    coverage_complete = false;
            for (const auto& issue : inventory.issues)
                if (!harmlessMountIssue(issue, inventory) && !supersededSignatureGap(issue) &&
                    !separateOwner(blockOwnershipKey(issue.node), key)) coverage_complete = false;
            bool signatures_complete = true;
            for (const auto& [child_key, child] : blocks) {
                const auto* child_trace = traces.at(child_key);
                if (std::none_of(child_trace->owners.begin(), child_trace->owners.end(),
                    [&](const auto& o) { return o.node == key; })) continue;
                const auto fresh = out.fresh_signatures.find(child_key);
                if (fresh == out.fresh_signatures.end() || !freshSignatureComplete(fresh->second) ||
                    fresh->second.state == FreshSignatureState::Membership ||
                    !child->observation.issues.empty() ||
                    std::any_of(child->issues.begin(), child->issues.end(),
                        [&](const auto& i) { return !supersededSignatureGap(i); }))
                    signatures_complete = false;
                if (fresh != out.fresh_signatures.end() && !freshSignatureComplete(fresh->second))
                    decision.reasons.push_back({Code::SignatureUnavailable, child_key, "libblkid:fresh",
                        freshSignatureStateName(fresh->second.state) + ": " + fresh->second.detail});
            }
            const bool clean = std::all_of(b->issues.begin(), b->issues.end(), supersededSignatureGap) &&
                               b->observation.capacity_bytes.value_or(0) > 0 && b->observation.read_only.has_value() &&
                               b->observation.read_only == false;
            if (!coverage_complete || unknown_usage)
                decision.reasons.push_back({Code::IncompleteCoverage, key, "host", "Host ownership coverage or an unattributed consumer is incomplete"});
            if (!signatures_complete)
                decision.reasons.push_back({Code::SignatureUnavailable, key, "signature", "No fresh successful signature/membership evidence for every descendant"});
            if (!clean)
                decision.reasons.push_back({Code::ConflictingEvidence, key, "inventory", "Missing flags or unresolved observation issues"});
            if (owner.scope == IdentityScope::NvmeNamespace)
                decision.reasons.push_back({Code::NamespaceScope, key, "identity", "Shared physical-controller ownership is not established"});
            if (coverage_complete && !unknown_usage && signatures_complete && clean &&
                owner.scope == IdentityScope::WholeDevice) {
                decision.status = DriveStatus::Ready;
                decision.reasons.push_back({Code::ChecksComplete, key, "classifier",
                    "Snapshot checks complete within observed host scope; no usage or ownership claim; not execution authorization"});
            }
        }
        if (decision.reasons.empty())
            decision.reasons.push_back({Code::IncompleteCoverage, key, "classifier", "Insufficient evidence"});
        normalize(decision.reasons);
        out.physical_devices.push_back(std::move(decision));
    }
    for (const auto& [key, node] : nodes) { (void)key; out.nodes.push_back(node); }
    normalize(out.issues);
    return out;
}
}  // namespace drivelab
