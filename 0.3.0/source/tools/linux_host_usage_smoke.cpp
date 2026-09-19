#include "platform/linux/linux_host_usage.h"
#include "platform/linux/linux_ownership_source.h"
#include "platform/linux/linux_inventory_provider.h"

#include <iostream>
#include <string_view>

namespace {
void text(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') std::cout << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) std::cout << "\\x" << hex[c >> 4] << hex[c & 15];
        else std::cout << static_cast<char>(c);
    }
    std::cout << '"';
}
void optional(const std::optional<std::string>& value) {
    if (value) text(*value); else std::cout << "unavailable";
}
const char* code(drivelab::DiscoveryIssueCode value) {
    using drivelab::DiscoveryIssueCode;
    switch (value) {
        case DiscoveryIssueCode::MissingEvidence: return "MissingEvidence";
        case DiscoveryIssueCode::MalformedEvidence: return "MalformedEvidence";
        case DiscoveryIssueCode::ReadFailure: return "ReadFailure";
        case DiscoveryIssueCode::Disappeared: return "Disappeared";
        case DiscoveryIssueCode::Conflict: return "Conflict";
        case DiscoveryIssueCode::AmbiguousIdentity: return "AmbiguousIdentity";
        case DiscoveryIssueCode::UnsupportedTopology: return "UnsupportedTopology";
        case DiscoveryIssueCode::UncorrelatedUsage: return "UncorrelatedUsage";
        case DiscoveryIssueCode::SnapshotChanged: return "SnapshotChanged";
    }
    return "Unrecognized";
}
void issues(const std::vector<drivelab::DiscoveryIssue>& values) {
    for (const auto& issue : values) {
        std::cout << "  issue=" << code(issue.code) << " node="; text(issue.node);
        std::cout << " field="; text(issue.field);
        std::cout << " errno=";
        if (issue.native_error) std::cout << *issue.native_error; else std::cout << "unavailable";
        if (issue.mount_id) std::cout << " mount_id=" << *issue.mount_id;
        std::cout << " message="; text(issue.message); std::cout << '\n';
    }
}
const char* signatureState(drivelab::SignatureState value) {
    using drivelab::SignatureState;
    switch (value) {
        case SignatureState::Unavailable: return "Unavailable";
        case SignatureState::Reported: return "Reported";
        case SignatureState::Malformed: return "Malformed";
        case SignatureState::Conflicting: return "Conflicting";
        case SignatureState::Unsupported: return "Unsupported";
    }
    return "Unrecognized";
}
}  // namespace

int main(int argc, char** argv) {
    const bool ownership = argc == 2 && std::string_view(argv[1]) == "--ownership";
    if (argc != 1 && !ownership) {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Usage: drivelab_host_usage_smoke [--ownership]\n"
                         "Development-only, read-only identity and host-usage observations.\n"
                         "--ownership adds C ownership paths and conservative snapshot classifications.\n"
                         "--ownership includes fresh read-only signature probes; no writes, storage commands,\n"
                         "execution authorization, or production mode.\n";
            return 0;
        }
        std::cerr << "Usage: drivelab_host_usage_smoke [--help|--ownership]\n"; return 2;
    }
    drivelab::UdevSysfsInventorySource source;
    drivelab::LinuxInventoryProvider inventory(source);
    drivelab::ProcHostFiles files;
    drivelab::UdevSignatureSource signatures;
    drivelab::LinuxResolvedInventoryProvider provider(inventory, files, signatures);
    const auto result = provider.scan();
    if (!result) {
        std::cout << "fatal="; text(result.error().message); std::cout << '\n'; return 1;
    }
    const auto& snapshot = result.value();
    std::cout << "DriveLab 0.3.0B development observations; NOT a safety decision.\n"
                 "Scope: current mount namespace; signatures are cached and freshness is unverified.\n"
                 "Scan is not atomic. Namespace identity does not resolve physical controller ownership.\n"
              << "nodes=" << snapshot.nodes.size()
              << " mountinfo_read_complete=" << snapshot.mountinfo_read_complete
              << " swaps_read_complete=" << snapshot.swaps_read_complete << '\n';
    for (const auto& node : snapshot.nodes) {
        std::cout << "\nnode="; text(node.observation.kernel_name);
        std::cout << " identity_state=";
        switch (node.identity.state) {
            case drivelab::ResolutionState::Resolved: std::cout << "Resolved"; break;
            case drivelab::ResolutionState::Unresolved: std::cout << "Unresolved"; break;
            case drivelab::ResolutionState::Ambiguous: std::cout << "Ambiguous"; break;
        }
        std::cout << " scope=" << (node.identity.scope == drivelab::IdentityScope::NvmeNamespace
                                      ? "NvmeNamespace" : "WholeDevice");
        std::cout << " id=";
        if (node.identity.id) text(node.identity.id->value); else std::cout << "unavailable";
        std::cout << " path="; optional(node.current_path); std::cout << '\n';
        for (const auto& alias : node.identity.strong_aliases) {
            std::cout << "  strong_alias="; text(alias); std::cout << '\n';
        }
        std::cout << "  signature=" << signatureState(node.signature.state) << " type=";
        optional(node.signature.type); std::cout << " usage="; optional(node.signature.usage);
        std::cout << " uuid="; optional(node.signature.uuid); std::cout << " label=";
        optional(node.signature.label); std::cout << '\n';
        for (const auto& child : node.usage_nodes) {
            std::cout << "  usage_node="; text(child); std::cout << '\n';
        }
        for (auto index : node.mounts) {
            std::cout << "  mount_id=" << snapshot.mounts[index].mount_id << '\n';
        }
        for (auto index : node.swaps) {
            std::cout << "  swap_path="; text(snapshot.swaps[index].path); std::cout << '\n';
        }
        for (const auto& observed : node.observation.model_observations) {
            std::cout << "  model_source="; text(observed.source);
            std::cout << " value="; text(observed.value); std::cout << '\n';
        }
        issues(node.issues);
        for (const auto& issue : node.observation.issues) {
            std::cout << "  raw_issue_code=" << static_cast<int>(issue.code) << " field=";
            text(issue.field); std::cout << " message="; text(issue.message); std::cout << '\n';
        }
    }
    for (const auto& mount : snapshot.mounts) {
        std::cout << "\nmount=" << mount.mount_id << " parent_mount=" << mount.parent_id
                  << " dev=" << mount.device_number.major_number << ':' << mount.device_number.minor_number
                  << " node="; optional(mount.node);
        std::cout << " root="; text(mount.root); std::cout << " point="; text(mount.mount_point);
        std::cout << " source="; text(mount.source); std::cout << " filesystem="; text(mount.filesystem_type);
        std::cout << " options="; text(mount.options);
        std::cout << " super_options="; text(mount.super_options); std::cout << '\n';
        for (const auto& field : mount.optional_fields) {
            std::cout << "  optional_field="; text(field); std::cout << '\n';
        }
    }
    for (const auto& swap : snapshot.swaps) {
        std::cout << "\nswap="; text(swap.path); std::cout << " type="; text(swap.type);
        std::cout << " size_kib=" << swap.size_kib << " used_kib=" << swap.used_kib
                  << " priority=" << swap.priority << " node="; optional(swap.node);
        std::cout << '\n';
    }
    issues(snapshot.issues);
    for (const auto& issue : snapshot.inventory_issues) {
        std::cout << "raw_issue_code=" << static_cast<int>(issue.code) << " field=";
        text(issue.field); std::cout << " message="; text(issue.message); std::cout << '\n';
    }
    if (ownership) {
        drivelab::NativeOwnershipIo io;
        drivelab::NativeZfsOwnershipSource zfs;
        drivelab::NativeFreshSignatureSource fresh_source(inventory, io);
        drivelab::LinuxOwnershipSource ownership_source(io, zfs, fresh_source);
        const auto assessment = drivelab::assessOwnership(snapshot, ownership_source.read(snapshot));
        std::cout << "\nDriveLab 0.3.0C ownership assessment; NOT execution authorization.\n"
                     "B virtual-node identity is unchanged; C owners are listed separately.\n";
        auto reason = [](const drivelab::OwnershipReason& r) {
            std::cout << "  reason=" << drivelab::ownershipReasonName(r.code) << " node=";
            text(r.node); std::cout << " source="; text(r.source);
            std::cout << " detail="; text(r.detail); std::cout << '\n';
        };
        for (const auto& [source_name, complete] : assessment.coverage) {
            std::cout << "coverage="; text(source_name); std::cout << " complete=" << complete << '\n';
        }
        for (const auto& [key, fresh] : assessment.fresh_signatures) {
            std::cout << "fresh_signature node="; text(key);
            std::cout << " state=" << drivelab::freshSignatureStateName(fresh.state);
            std::cout << " type="; text(fresh.type); std::cout << " usage="; text(fresh.usage);
            std::cout << " partition_table="; text(fresh.partition_table);
            std::cout << " detail="; text(fresh.detail);
            std::cout << " errno="; if (fresh.native_error) std::cout << *fresh.native_error; else std::cout << "unavailable";
            std::cout << '\n';
        }
        for (const auto& observation : assessment.namespace_mounts) {
            std::cout << "namespace_mount namespace="; text(observation.namespace_id);
            std::cout << " mount_id=" << observation.mount.mount_id << " node="; optional(observation.mount.node);
            std::cout << " target="; text(observation.mount.mount_point); std::cout << '\n';
        }
        for (const auto& [source_name,gaps] : assessment.coverage_gaps) {
            for (const auto& gap : gaps) {
                std::cout << "coverage_gap source="; text(source_name);
                std::cout << " scope=" << (gap.node.empty() ? "global" : "backing-chain");
                reason(gap);
            }
        }
        for (const auto& node : assessment.nodes) {
            std::cout << "graph_node="; text(node.key); std::cout << " complete=" << node.complete;
            for (const auto& [key, value] : node.facts) {
                std::cout << " "; text(key); std::cout << "="; text(value);
            }
            std::cout << '\n';
        }
        for (const auto& edge : assessment.edges) {
            std::cout << "edge consumer="; text(edge.consumer); std::cout << " provider="; text(edge.provider);
            std::cout << " relation="; text(edge.relation); std::cout << " source="; text(edge.source);
            std::cout << '\n';
        }
        for (const auto& trace : assessment.traces) {
            std::cout << "trace consumer="; text(trace.consumer); std::cout << " complete=" << trace.complete << '\n';
            for (const auto& owner : trace.owners) {
                std::cout << "  physical_owner="; text(owner.node); std::cout << " id=";
                if (owner.id) text(owner.id->value); else std::cout << "unavailable";
                std::cout << '\n';
            }
            for (const auto& r : trace.reasons) reason(r);
        }
        for (const auto& decision : assessment.physical_devices) {
            std::cout << "classification node="; text(decision.node); std::cout << " status=";
            switch (decision.status) {
                case drivelab::DriveStatus::Ready: std::cout << "READY"; break;
                case drivelab::DriveStatus::Busy: std::cout << "BUSY"; break;
                case drivelab::DriveStatus::Protected: std::cout << "PROTECTED"; break;
                default: std::cout << "UNKNOWN"; break;
            }
            std::cout << '\n';
            for (const auto& r : decision.reasons) reason(r);
        }
        std::cout << "global_ownership_issues count=" << assessment.issues.size() << '\n';
        for (const auto& r : assessment.issues) {
            std::cout << "global_issue";
            reason(r);
        }
    }
    return std::cout ? 0 : 1;
}
