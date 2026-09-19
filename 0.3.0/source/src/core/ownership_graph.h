#pragma once

#include "core/resolved_inventory.h"

#include <map>

namespace drivelab {

enum class OwnershipKind { Block, LvmPv, LvmVg, LvmLv, MdArray, ZfsPool, ZfsVdev,
                           ProxmoxStorage, ProxmoxGuest };
enum class OwnershipUse { Held, Mounted, Root, Boot, Home, Swap, Lvm, Raid,
                          Zfs, ConfiguredStorage, Guest, OpenBlockHandle, OpenFileHandle };
enum class OwnershipReasonCode {
    ChecksComplete, IdentityUnavailable, PathUnavailable, ReadOnly, Mount,
    SystemRoot, BootFilesystem, HomeFilesystem, ActiveSwap, Holder, LvmMember,
    RaidMember, ZfsMember, ConfiguredStorage, GuestAssignment, OpenBlockHandle, OpenFileHandle,
    MissingSource, MissingRelationship, AsymmetricRelationship, DuplicateNode,
    Cycle, UnsupportedTopology, ConflictingEvidence, SnapshotChanged,
    IncompleteCoverage, SignatureUnavailable, NamespaceScope
};

struct OwnershipReason {
    OwnershipReasonCode code = OwnershipReasonCode::IncompleteCoverage;
    std::string node;
    std::string source;
    std::string detail;
    friend bool operator==(const OwnershipReason&, const OwnershipReason&) = default;
};

struct OwnershipNode {
    std::string key;
    OwnershipKind kind = OwnershipKind::Block;
    std::map<std::string, std::string> facts;
    bool complete = true;
    friend bool operator==(const OwnershipNode&, const OwnershipNode&) = default;
};

// Edges point from a consumer towards its backing provider, never a DriveId.
struct OwnershipEdge {
    std::string consumer;
    std::string provider;
    std::string relation;
    std::string source;
    friend bool operator==(const OwnershipEdge&, const OwnershipEdge&) = default;
};

struct OwnershipClaim {
    std::string consumer;
    OwnershipUse use = OwnershipUse::Held;
    std::string source;
    std::string detail;
    friend bool operator==(const OwnershipClaim&, const OwnershipClaim&) = default;
};

enum class FreshSignatureState {
    Unavailable, None, Filesystem, Membership, Unsupported,
    PermissionDenied, ReadFailure, Disappeared, Conflicting
};
struct FreshPartition {
    int number = 0;
    std::uint64_t start_sector = 0, size_sectors = 0;
    friend bool operator==(const FreshPartition&, const FreshPartition&) = default;
};
struct FreshSignatureEvidence {
    FreshSignatureState state = FreshSignatureState::Unavailable;
    std::string type, usage, partition_table;
    std::vector<FreshPartition> partitions;
    std::optional<int> native_error;
    std::string detail;
    friend bool operator==(const FreshSignatureEvidence&, const FreshSignatureEvidence&) = default;
};
using FreshSignatures = std::map<std::string, FreshSignatureEvidence>;
bool freshSignatureComplete(const FreshSignatureEvidence& evidence);
std::string freshSignatureStateName(FreshSignatureState state);
// Recognizes the pmxcfs interface only; native process coverage must separately
// attribute and revalidate its persistent database backing.
bool pmxcfsOwnershipMount(const MountEvidence& mount);
bool harmlessOwnershipMount(const MountEvidence& mount);

struct NamespaceMount {
    std::string namespace_id;
    MountEvidence mount;
    friend bool operator==(const NamespaceMount&, const NamespaceMount&) = default;
};

struct OwnershipEvidence {
    std::vector<OwnershipNode> nodes;
    std::vector<OwnershipEdge> edges;
    std::vector<OwnershipClaim> claims;
    std::vector<OwnershipReason> issues;
    FreshSignatures fresh_signatures; // block key -> fresh, revalidated result
    std::vector<NamespaceMount> namespace_mounts;
    // Only positively observed inactive zero-size loop nodes may be excluded.
    std::vector<std::string> inactive_loops;
    // Exhaustive source gap records, separate from structural graph failures.
    // An empty node is unbounded; a node scopes only through complete backing.
    std::map<std::string, std::vector<OwnershipReason>> coverage_gaps;
    // Source summaries; inactive membership is evaluated per target/descendant.
    // false also represents unavailable/not built/permission failure.
    std::map<std::string, bool> coverage;
};

struct PhysicalOwner {
    std::string node;
    std::optional<DriveId> id;
    IdentityScope scope = IdentityScope::WholeDevice;
    friend bool operator==(const PhysicalOwner&, const PhysicalOwner&) = default;
};

struct OwnershipTrace {
    std::string consumer;
    std::vector<PhysicalOwner> owners;
    bool complete = false;
    std::vector<OwnershipReason> reasons;
    friend bool operator==(const OwnershipTrace&, const OwnershipTrace&) = default;
};

struct ClassifiedStorage {
    std::string node;
    std::optional<DriveId> id;
    DriveStatus status = DriveStatus::Unknown;
    std::vector<OwnershipReason> reasons;
    friend bool operator==(const ClassifiedStorage&, const ClassifiedStorage&) = default;
};

struct OwnershipAssessment {
    std::map<std::string, std::vector<OwnershipReason>> coverage_gaps;
    FreshSignatures fresh_signatures;
    std::vector<NamespaceMount> namespace_mounts;
    std::vector<OwnershipNode> nodes;
    std::vector<OwnershipEdge> edges;
    std::vector<OwnershipClaim> claims;
    std::vector<OwnershipTrace> traces;
    std::vector<ClassifiedStorage> physical_devices;
    std::vector<OwnershipReason> issues;
    std::map<std::string, bool> coverage;
    friend bool operator==(const OwnershipAssessment&, const OwnershipAssessment&) = default;
};

std::string blockOwnershipKey(const std::string& kernel_name);
std::string ownershipReasonName(OwnershipReasonCode code);
// No execution authorization or application integration is performed.
// Graph endpoints are always retained; this only scopes final candidate decisions.
enum class PhysicalAssessmentScope { AllPhysical, Disks };
OwnershipAssessment assessOwnership(const ResolvedInventorySnapshot& inventory,
                                    OwnershipEvidence evidence,
                                    PhysicalAssessmentScope scope = PhysicalAssessmentScope::AllPhysical);

}  // namespace drivelab
