#include "ownership_fixture.h"
#include <random>

using namespace ownership_test;
namespace {
OwnershipEvidence readyEvidence(Fixture& f) {
    auto e = f.evidence();
    // Inject structured fresh outcomes, never just a complete-coverage flag.
    for (const auto& b : f.inventory.nodes) {
        FreshSignatureEvidence fresh;
        fresh.state = FreshSignatureState::Filesystem; fresh.type = "ext4"; fresh.usage = "filesystem";
        e.fresh_signatures[blockOwnershipKey(b.observation.kernel_name)] = fresh;
    }
    e.coverage["inactive_membership"] = true;
    std::erase_if(e.issues, [](const auto& i) { return i.source == "inactive_membership"; });
    return e;
}
void classifications() {
    Fixture f;
    auto a = assessOwnership(f.inventory, readyEvidence(f));
    DL_CHECK(device(a).status == DriveStatus::Ready);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ChecksComplete));
    auto e = f.evidence();
    e.claims.push_back({"block:sda", OwnershipUse::OpenBlockHandle, "fixture", "handle"});
    a = assessOwnership(f.inventory, e);
    DL_CHECK(device(a).status == DriveStatus::Busy);
    DL_CHECK(reason(device(a), OwnershipReasonCode::OpenBlockHandle));
    f.mount("sda", "/");
    a = f.assess();
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::SystemRoot));
    f.node("sda").identity = {};
    a = f.assess();
    DL_CHECK(device(a).status == DriveStatus::Unknown);
    DL_CHECK(reason(device(a), OwnershipReasonCode::IdentityUnavailable));
}
void noIncompleteReady() {
    Fixture f;
    const auto base = readyEvidence(f);
    for (const auto* coverage : {"sysfs", "zfs", "proxmox", "processes"}) {
        auto e = base; e.coverage[coverage] = false;
        DL_CHECK(device(assessOwnership(f.inventory, e)).status == DriveStatus::Unknown);
    }
    f.inventory.mountinfo_read_complete = false;
    DL_CHECK(device(assessOwnership(f.inventory, readyEvidence(f))).status == DriveStatus::Unknown);
    f.inventory.mountinfo_read_complete = true;
    auto missing_probe = base;
    missing_probe.fresh_signatures.clear();
    DL_CHECK(device(assessOwnership(f.inventory, missing_probe)).status == DriveStatus::Unknown);
    f.node("sda").signature.state = SignatureState::Unavailable;
    DL_CHECK(device(assessOwnership(f.inventory, readyEvidence(f))).status == DriveStatus::Ready);
    f.node("sda").signature.state = SignatureState::Reported;
    f.node("sda").identity.scope = IdentityScope::NvmeNamespace;
    DL_CHECK(device(assessOwnership(f.inventory, readyEvidence(f))).status == DriveStatus::Unknown);
    f.node("sda").identity.scope = IdentityScope::WholeDevice;
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);
    auto e = base;
    e.nodes.push_back({"unattributed", OwnershipKind::Block, {}, false});
    DL_CHECK(device(assessOwnership(f.inventory, e)).status == DriveStatus::Unknown);
    e = base;
    e.edges.push_back({"missing-consumer", "block:sda", "broken", "fixture"});
    DL_CHECK(device(assessOwnership(f.inventory, e)).status != DriveStatus::Ready);
}
void harmlessMountCoverage() {
    for (const auto* fs : {"proc", "sysfs", "tmpfs", "devtmpfs", "devpts", "cgroup", "cgroup2",
                          "securityfs", "debugfs", "tracefs", "configfs", "pstore", "hugetlbfs",
                          "mqueue", "bpf", "fusectl", "binfmt_misc", "rpc_pipefs", "autofs"}) {
        Fixture f;
        auto e = readyEvidence(f); // Explicit injected authority ONLY.
        MountEvidence m; m.mount_id = 91; m.device_number = {0, 22};
        m.filesystem_type = fs; m.source = fs; m.root = "/"; m.mount_point = "/pseudo";
        f.inventory.mounts.push_back(m);
        f.inventory.issues.push_back({DiscoveryIssueCode::UncorrelatedUsage, "", "mountinfo",
                                     "Message wording is deliberately irrelevant", {}, 91});
        DL_CHECK(device(assessOwnership(f.inventory, e)).status == DriveStatus::Ready);
        DL_CHECK(device(f.assess()).status == DriveStatus::Unknown); // Native policy unchanged.
        auto checkUnknown = [&] { DL_CHECK(device(assessOwnership(f.inventory, e)).status == DriveStatus::Unknown); };
        auto& issue = f.inventory.issues[0];
        issue.mount_id.reset(); checkUnknown(); issue.mount_id = 91;
        issue.mount_id = 92; checkUnknown(); issue.mount_id = 91;
        issue.native_error = 13; checkUnknown(); issue.native_error.reset();
        for (const auto code : {DiscoveryIssueCode::ReadFailure, DiscoveryIssueCode::Conflict,
                               DiscoveryIssueCode::UnsupportedTopology, DiscoveryIssueCode::SnapshotChanged,
                               DiscoveryIssueCode::MalformedEvidence, DiscoveryIssueCode::AmbiguousIdentity}) {
            issue.code = code; checkUnknown();
        }
        issue.code = DiscoveryIssueCode::UncorrelatedUsage;
        f.inventory.mounts[0].source = "/dev/unresolved"; checkUnknown();
        f.inventory.mounts[0].source = fs;
        f.inventory.mounts[0].device_number = {8, 55}; checkUnknown();
        f.inventory.mounts[0].device_number = {0, 22};
        f.inventory.mounts[0].filesystem_type = "ext4"; checkUnknown();
        f.inventory.mounts[0].filesystem_type = fs;
        f.inventory.mounts.push_back(m); checkUnknown(); f.inventory.mounts.pop_back();
        f.inventory.mountinfo_read_complete = false; checkUnknown();
        f.inventory.mountinfo_read_complete = true;
        // A pseudo-looking record with ambiguous block candidates is not harmless.
        f.node("sda").observation.device_number = m.device_number; checkUnknown();
    }
}
void sharedGuestAttribution() {
    Fixture f; f.add("sdb", {8, 16}); f.pve();
    f.guest("scsi0: /dev/sda\nscsi1: /dev/sdb\n");
    f.mount("sda", "/");
    f.node("sdb").identity = {};
    auto a = f.assess();
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::SystemRoot));
    DL_CHECK(reason(device(a), OwnershipReasonCode::GuestAssignment));
    DL_CHECK(!reason(device(a), OwnershipReasonCode::IdentityUnavailable));
    DL_CHECK(device(a, "sdb").status == DriveStatus::Unknown);
    DL_CHECK(reason(device(a, "sdb"), OwnershipReasonCode::IdentityUnavailable));
    const auto guest = trace(a, "pve-guest:qemu-server:100");
    DL_CHECK(!guest.complete && guest.owners.size() == 2 && !guest.reasons.empty());
    f.node("sdb").current_path.reset();
    a = f.assess();
    DL_CHECK(!reason(device(a), OwnershipReasonCode::PathUnavailable));
    DL_CHECK(reason(device(a, "sdb"), OwnershipReasonCode::PathUnavailable));
    std::reverse(f.inventory.nodes.begin(), f.inventory.nodes.end());
    DL_CHECK(f.assess() == a);
    // An unresolved backing branch cannot be assigned exclusively to the sibling.
    auto e = f.evidence();
    e.nodes.push_back({"unresolved-backend", OwnershipKind::Block, {}, false});
    e.edges.push_back({"pve-guest:qemu-server:100", "unresolved-backend", "guest-assignment", "fixture"});
    a = assessOwnership(f.inventory, e);
    DL_CHECK(device(a).status == DriveStatus::Unknown);
    DL_CHECK(reason(device(a), OwnershipReasonCode::MissingRelationship));
    DL_CHECK(device(a, "sdb").status == DriveStatus::Unknown);
    // Shared consumer uncertainty (for example unsupported PCI) stays shared.
    e = f.evidence();
    e.issues.push_back({OwnershipReasonCode::IncompleteCoverage, "pve-guest:qemu-server:100",
                        "hostpci0", "No trustworthy storage owner"});
    a = assessOwnership(f.inventory, e);
    DL_CHECK(device(a).status == DriveStatus::Unknown);
    DL_CHECK(reason(device(a), OwnershipReasonCode::IncompleteCoverage));
    DL_CHECK(device(a, "sdb").status == DriveStatus::Unknown);
    // Both paths reach one endpoint: its identity failure must never be filtered.
    Fixture shared; shared.partition("sda1", "sda", {8, 1}); shared.pve();
    shared.guest("scsi0: /dev/sda\nscsi1: /dev/sda1\n"); shared.node("sda").identity = {};
    DL_CHECK(reason(device(shared.assess()), OwnershipReasonCode::IdentityUnavailable));
}
void scopedCoverageGaps() {
    Fixture f; f.add("sdb",{8,16});
    auto e=readyEvidence(f);
    e.coverage["processes"]=false;
    e.coverage_gaps["processes"]={{OwnershipReasonCode::IncompleteCoverage,"block:sdb","proc","Known file backing"}};
    auto a=assessOwnership(f.inventory,e);
    DL_CHECK(device(a).status == DriveStatus::Ready);
    DL_CHECK(device(a,"sdb").status == DriveStatus::Unknown);
    e.coverage_gaps["processes"].front().node = "missing-owner";
    DL_CHECK(device(assessOwnership(f.inventory,e)).status == DriveStatus::Unknown);
    e.coverage_gaps["processes"].front().node.clear();
    DL_CHECK(device(assessOwnership(f.inventory,e)).status == DriveStatus::Unknown);
    e.coverage_gaps["processes"].clear();
    DL_CHECK(device(assessOwnership(f.inventory,e)).status == DriveStatus::Unknown);
}
void topologyAndOrdering() {
    Fixture f;
    f.partition("sda1", "sda", {8, 1});
    f.dm("dm-0", 0, "sda1");
    f.dm("dm-1", 1, "sda1");
    f.mount("dm-1", "/"); f.swap("dm-0");
    const auto evidence = f.evidence();
    const auto reference = assessOwnership(f.inventory, evidence);
    std::mt19937 random(1030);
    for (int n = 0; n < 30; ++n) {
        auto inventory = f.inventory; auto e = evidence;
        std::shuffle(inventory.nodes.begin(), inventory.nodes.end(), random);
        std::shuffle(e.nodes.begin(), e.nodes.end(), random);
        std::shuffle(e.edges.begin(), e.edges.end(), random);
        std::shuffle(e.claims.begin(), e.claims.end(), random);
        std::shuffle(e.issues.begin(), e.issues.end(), random);
        DL_CHECK(assessOwnership(inventory, e) == reference);
    }
    auto e = evidence;
    e.edges.push_back({"block:dm-0", "block:dm-1", "cycle", "fixture"});
    e.edges.push_back({"block:dm-1", "block:dm-0", "cycle", "fixture"});
    auto a = assessOwnership(f.inventory, e);
    DL_CHECK(device(a).status == DriveStatus::Unknown);
    DL_CHECK(reason(device(a), OwnershipReasonCode::Cycle));
    e = evidence; e.nodes.push_back(e.nodes.front());
    DL_CHECK(device(assessOwnership(f.inventory, e)).status == DriveStatus::Unknown);
    e = evidence; f.node("sda").observation.child_kernel_names.clear();
    DL_CHECK(device(assessOwnership(f.inventory, e)).status != DriveStatus::Ready);
}
}
int main() {
    return drivelab::test::run([] { classifications(); noIncompleteReady(); harmlessMountCoverage(); sharedGuestAttribution(); scopedCoverageGaps(); topologyAndOrdering(); });
}
