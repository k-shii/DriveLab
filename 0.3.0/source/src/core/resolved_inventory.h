#pragma once

#include "core/block_inventory.h"
#include "core/drive.h"

#include <optional>
#include <string>
#include <vector>

namespace drivelab {

// These are resolution/evidence states, never production safety states.
enum class ResolutionState { Unresolved, Resolved, Ambiguous };
enum class IdentityScope { WholeDevice, NvmeNamespace };
enum class DiscoveryIssueCode {
    MissingEvidence, MalformedEvidence, ReadFailure, Disappeared, Conflict,
    AmbiguousIdentity, UnsupportedTopology, UncorrelatedUsage, SnapshotChanged
};

struct DiscoveryIssue {
    DiscoveryIssueCode code;
    std::string node;
    std::string field;
    std::string message;
    std::optional<int> native_error = std::nullopt;
    // Structured correlation; never recover mount identity from message text.
    std::optional<std::uint64_t> mount_id = std::nullopt;
    friend bool operator==(const DiscoveryIssue&, const DiscoveryIssue&) = default;
};

struct ResolvedIdentity {
    ResolutionState state = ResolutionState::Unresolved;
    IdentityScope scope = IdentityScope::WholeDevice;
    std::optional<DriveId> id;
    // All validated strong aliases participate in collision checks.
    std::vector<std::string> strong_aliases;
    std::optional<std::string> fallback_fingerprint;
    friend bool operator==(const ResolvedIdentity&, const ResolvedIdentity&) = default;
};

enum class SignatureState { Unavailable, Reported, Malformed, Conflicting, Unsupported };
struct SignatureEvidence {
    SignatureState state = SignatureState::Unavailable;
    // Cached OS observations, not a fresh probe or proof of absence.
    std::string provenance = "udev database; freshness unverified";
    std::optional<std::string> type;
    std::optional<std::string> usage;
    std::optional<std::string> uuid;
    std::optional<std::string> label;
    friend bool operator==(const SignatureEvidence&, const SignatureEvidence&) = default;
};

struct MountEvidence {
    std::uint64_t mount_id = 0;
    std::uint64_t parent_id = 0;
    BlockDeviceNumber device_number;
    std::string root;
    std::string mount_point;
    std::string options;
    std::vector<std::string> optional_fields;
    std::string filesystem_type;
    std::string source;
    std::string super_options;
    std::optional<std::string> node;
    // Non-root subtrees may be bind mounts or filesystem subvolumes.
    bool subtree = false;
    friend bool operator==(const MountEvidence&, const MountEvidence&) = default;
};

struct SwapEvidence {
    std::string path;
    std::string type;
    std::uint64_t size_kib = 0;
    std::uint64_t used_kib = 0;
    int priority = 0;
    std::optional<BlockDeviceNumber> device_number;
    std::optional<std::string> node;
    friend bool operator==(const SwapEvidence&, const SwapEvidence&) = default;
};

struct ResolvedBlockNode {
    BlockDeviceObservation observation;
    ResolvedIdentity identity;
    // Validated current locator only; never part of the stable identifier.
    std::optional<std::string> current_path;
    SignatureEvidence signature;
    // Indices into snapshot evidence. Whole nodes also include direct children.
    std::vector<std::size_t> mounts;
    std::vector<std::size_t> swaps;
    std::vector<std::string> usage_nodes;
    std::vector<DiscoveryIssue> issues;
    friend bool operator==(const ResolvedBlockNode&, const ResolvedBlockNode&) = default;
};

struct ResolvedInventorySnapshot {
    std::vector<ResolvedBlockNode> nodes;
    std::vector<MountEvidence> mounts;
    std::vector<SwapEvidence> swaps;
    std::vector<BlockInventoryIssue> inventory_issues;
    std::vector<DiscoveryIssue> issues;
    bool mountinfo_read_complete = false;
    bool swaps_read_complete = false;
    // Reading /proc/self/mountinfo describes only the caller's mount namespace.
    friend bool operator==(const ResolvedInventorySnapshot&,
                           const ResolvedInventorySnapshot&) = default;
};

// Pure snapshot resolution; does not acquire host state or construct Drive/status.
ResolvedInventorySnapshot resolveBlockIdentities(const BlockInventorySnapshot& raw);
Result<std::string> resolveCurrentPath(const ResolvedInventorySnapshot& snapshot,
                                      const DriveId& expected);
void sortDiscoveryIssues(std::vector<DiscoveryIssue>& issues);

}  // namespace drivelab
