#include "platform/linux/linux_ownership_source.h"
#include "core/scan_profile.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <functional>
#include <set>
#include <sstream>

namespace drivelab {
namespace {
using Code = OwnershipReasonCode;
bool component(const std::string& s) {
    return !s.empty() && s != "." && s != ".." &&
        std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!';
        });
}
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \r\n\t") - first + 1);
}
bool deviceNumber(const std::string& text, BlockDeviceNumber expected) {
    const auto s = trim(text);
    const auto colon = s.find(':');
    if (colon == std::string::npos) return false;
    std::uint32_t major = 0, minor = 0;
    const auto a = std::from_chars(s.data(), s.data() + colon, major);
    const auto b = std::from_chars(s.data() + colon + 1, s.data() + s.size(), minor);
    return a.ec == std::errc{} && b.ec == std::errc{} &&
        a.ptr == s.data() + colon && b.ptr == s.data() + s.size() &&
        BlockDeviceNumber{major, minor} == expected;
}
std::optional<std::vector<std::string>> dmNames(const std::string& text) {
    std::vector<std::string> names(1);
    const auto name = trim(text);
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] != '-') names.back() += name[i];
        else if (i + 1 < name.size() && name[i + 1] == '-') { names.back() += '-'; ++i; }
        else names.emplace_back();
    }
    if (names.size() < 2 || names.size() > 3 ||
        std::any_of(names.begin(), names.end(), [](const auto& n) { return !component(n); })) return {};
    return names;
}
bool lvmId(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(),
        [](unsigned char c) { return std::isalnum(c) != 0; });
}
struct SysNode {
    std::vector<std::string> slaves, holders;
    std::string uuid, name;
    bool dm = false, md = false;
};
}
void OwnershipScanContext::issue(Code code, const std::string& node,
                                  const std::string& source, const std::string& detail) {
    evidence.issues.push_back({code, node, source, detail});
}
bool OwnershipScanContext::accountZfsControl(const OwnershipPath& path) {
    if (path.block || path.filesystem_object || !path.character_device ||
        !zfs_control_number || !evidence.coverage.contains("zfs") || !evidence.coverage.at("zfs") ||
        !deviceNumber(*zfs_control_number,*path.character_device)) return false;
    // This control object's state is accounted by the ZFS provider, not assigned
    // as an open block/file reference to an arbitrarily chosen pool.
    zfs_control_observed = true;
    return true;
}
bool OwnershipScanContext::accountSocket(const OwnershipPath& path, const std::string& source) {
    if (!path.socket || path.block || path.filesystem_object || path.character_device ||
        !source.starts_with("/proc/")) return false;
    const auto fd = source.rfind("/fd/");
    if (fd == std::string::npos) return false;
    const auto file = source.substr(0,fd) + "/fdinfo/" + source.substr(fd+4);
    auto emptyRights = [](const OwnershipRead<std::string>& read) {
        if (!read.value || read.value->empty() || read.value->size()>4*1024*1024 ||
            read.value->back()!='\n') return false;
        std::istringstream lines(*read.value); std::string line; bool found=false;
        while (std::getline(lines,line)) if (line.starts_with("scm_fds:")) {
            if (found || trim(line.substr(8)) != "0") return false;
            found=true;
        }
        return found;
    };
    // Unix socket queues can retain SCM_RIGHTS files after visible FDs close.
    // An absent field does not prove a non-Unix socket or an empty queue.
    const auto before=io.text(file), after=io.text(file);
    return emptyRights(before) && emptyRights(after);
}
std::optional<std::string> OwnershipScanContext::pathOwner(const std::string& path) {
    const auto before = io.locate(path);
    const auto after = io.locate(path);
    if (!before.value || !after.value || *before.value != *after.value) {
        issue(Code::SnapshotChanged, "", path, "Path metadata unavailable or changed");
        return {};
    }
    std::set<std::string> owners;
    if (before.value->block) {
        for (const auto& b : inventory.nodes)
            if (b.observation.device_number == before.value->device)
                owners.insert(blockOwnershipKey(b.observation.kernel_name));
    } else {
        for (const auto& m : inventory.mounts) {
            if (m.device_number != before.value->device) continue;
            if (m.node) owners.insert(blockOwnershipKey(*m.node));
            else if (m.filesystem_type == "zfs") {
                const auto pool = m.source.substr(0, m.source.find('/'));
                if (pools.contains(pool)) owners.insert(pools.at(pool));
            }
        }
    }
    if (owners.size() == 1) return *owners.begin();
    issue(Code::MissingRelationship, "", path, "Path has no unique observed block or ZFS filesystem owner");
    return {};
}

OwnershipEvidence LinuxOwnershipSource::read(const ResolvedInventorySnapshot& inventory) {
    ScanStage timing("ownership.total");
    OwnershipEvidence out;
    out.coverage = {{"sysfs", true}, {"zfs", false}, {"proxmox", false}, {"processes", false},
                    {"inactive_membership", false}};
    out.issues.push_back({Code::IncompleteCoverage, "", "inactive_membership",
        "Cached udev signatures and active kernel mappings do not establish absence of inactive LVM/md/ZFS ownership"});
    OwnershipScanContext context{io_, inventory, out, {}, {}, std::nullopt, false};
    std::map<std::string, const ResolvedBlockNode*> blocks;
    std::map<std::string, SysNode> sys;
    std::map<std::string, OwnershipRead<std::string>> texts;
    std::map<std::string, OwnershipRead<std::vector<std::string>>> lists;
    auto text = [&](const std::string& path) {
        const auto r = io_.text(path);
        texts[path] = r;
        return r;
    };
    auto list = [&](const std::string& path) {
        auto r = io_.list(path);
        if (r.value) std::sort(r.value->begin(), r.value->end());
        lists[path] = r;
        return r;
    };
    auto issue = [&](Code code, const std::string& name, const std::string& detail) {
        out.coverage["sysfs"] = false;
        context.issue(code, name.empty() ? "" : blockOwnershipKey(name), "sysfs", detail);
    };
    const auto topology_start = std::chrono::steady_clock::now();
    std::set<std::string> names;
    for (const auto& b : inventory.nodes) {
        const auto& name = b.observation.kernel_name;
        if (!component(name) || !blocks.emplace(name, &b).second) {
            issue(Code::ConflictingEvidence, name, "Invalid or duplicate kernel name");
            continue;
        }
        names.insert(name);
    }
    const auto enumeration = list("/sys/class/block");
    if (!enumeration.value || std::set<std::string>(enumeration.value->begin(), enumeration.value->end()) != names)
        issue(Code::SnapshotChanged, "", "Block enumeration differs from B snapshot");

    for (const auto& [name, b] : blocks) {
        const auto base = "/sys/class/block/" + name;
        const auto dev = text(base + "/dev");
        if (!dev.value || !b->observation.device_number ||
            !deviceNumber(*dev.value, *b->observation.device_number))
            issue(Code::SnapshotChanged, name, "Missing or changed device number");
        auto& node = sys[name];
        for (const auto& relation : {"slaves", "holders"}) {
            const auto r = list(base + "/" + relation);
            if (!r.value) {
                // Linux creates holders for partitions, but slaves only for
                // whole gendisks. A partition's backing edge is partition-of.
                if (r.absent && std::string(relation) == "slaves" &&
                    b->observation.node_type == BlockNodeType::Partition) continue;
                issue(Code::MissingSource, name, std::string("Unreadable ") + relation);
                continue;
            }
            auto& target = std::string(relation) == "slaves" ? node.slaves : node.holders;
            target = *r.value;
            if (std::adjacent_find(target.begin(), target.end()) != target.end() ||
                std::any_of(target.begin(), target.end(), [](const auto& n) { return !component(n); }))
                issue(Code::ConflictingEvidence, name, "Invalid or duplicate relationship name");
        }
        const auto dm = list(base + "/dm");
        node.dm = dm.value.has_value();
        if (!dm.value && !dm.absent) issue(Code::MissingSource, name, "Cannot inspect device-mapper metadata");
        if (node.dm) {
            const auto uuid = text(base + "/dm/uuid"), dm_name = text(base + "/dm/name");
            if (!uuid.value || !dm_name.value) issue(Code::MissingSource, name, "Unreadable device-mapper identity metadata");
            else { node.uuid = trim(*uuid.value); node.name = trim(*dm_name.value); }
            if (node.slaves.empty()) issue(Code::MissingRelationship, name, "Device-mapper has no observed backing device");
        }
        const auto md = list(base + "/md");
        node.md = md.value.has_value();
        if (!md.value && !md.absent) issue(Code::MissingSource, name, "Cannot inspect md metadata");
        if (node.md) {
            const auto state = text(base + "/md/array_state");
            const auto level = text(base + "/md/level");
            const auto count = text(base + "/md/raid_disks");
            const auto degraded = text(base + "/md/degraded");
            const std::set<std::string> levels{"linear", "raid0", "raid1", "raid4", "raid5", "raid6", "raid10", "multipath"};
            std::size_t members = 0;
            const auto count_string = count.value ? trim(*count.value) : "";
            const auto parsed = std::from_chars(count_string.data(), count_string.data() + count_string.size(), members);
            if (!state.value || !level.value || !degraded.value || trim(*degraded.value) != "0" ||
                !levels.contains(trim(*level.value)) ||
                (trim(*state.value) != "active" && trim(*state.value) != "clean" &&
                 trim(*state.value) != "readonly" && trim(*state.value) != "read-auto" &&
                 trim(*state.value) != "active-idle") ||
                parsed.ec != std::errc{} || parsed.ptr != count_string.data() + count_string.size() ||
                members == 0 || node.slaves.size() < members)
                issue(Code::IncompleteCoverage, name, "Incomplete, degraded, inactive, or unsupported md array");
            out.claims.push_back({blockOwnershipKey(name), OwnershipUse::Raid, "sysfs:md", "Kernel md membership"});
            const auto key = "md:" + name;
            out.nodes.push_back({key, OwnershipKind::MdArray, {{"level", level.value ? trim(*level.value) : ""}}, true});
            out.edges.push_back({key, blockOwnershipKey(name), "array-node", "sysfs:md"});
        }
        // Linux major 7 identifies loop devices. Only an explicitly detached,
        // zero-size loop with empty relations is independent of physical storage.
        if (b->observation.observation_kind == BlockObservationKind::VirtualOrPseudo &&
            b->observation.device_number && b->observation.device_number->major_number == 7) {
            const auto loop = list(base + "/loop");
            const auto size = text(base + "/size");
            if (loop.absent && size.value && trim(*size.value) == "0" &&
                b->observation.capacity_bytes == 0 && node.slaves.empty() && node.holders.empty())
                out.inactive_loops.push_back(blockOwnershipKey(name));
        }
        if (!node.dm && !node.md && !node.slaves.empty())
            issue(Code::UnsupportedTopology, name, "Backing links on an unsupported virtual layer");
    }
    // Retain observed edges even when asymmetric, but taint both endpoints.
    // Otherwise a missing reverse link could accidentally leave a provider READY.
    for (const auto& [name, node] : sys) {
        for (const auto& slave : node.slaves) {
            out.edges.push_back({blockOwnershipKey(name), blockOwnershipKey(slave), "slave", "sysfs"});
            out.claims.push_back({blockOwnershipKey(name), OwnershipUse::Held, "sysfs", "Kernel holder"});
            if (!sys.contains(slave) ||
                std::count(sys[slave].holders.begin(), sys[slave].holders.end(), name) != 1) {
                issue(Code::AsymmetricRelationship, name, "Slave missing reciprocal holder: " + slave);
                issue(Code::AsymmetricRelationship, slave, "Holder missing reciprocal link: " + name);
            }
        }
        for (const auto& holder : node.holders) {
            out.edges.push_back({blockOwnershipKey(holder), blockOwnershipKey(name), "holder", "sysfs"});
            if (!sys.contains(holder) ||
                std::count(sys[holder].slaves.begin(), sys[holder].slaves.end(), name) != 1) {
                issue(Code::AsymmetricRelationship, name, "Holder missing reciprocal slave: " + holder);
                issue(Code::AsymmetricRelationship, holder, "Slave missing reciprocal link: " + name);
            }
        }
    }
    // Active LVM UUIDs carry fixed-length VG/LV IDs. Backing PV candidates come
    // exclusively from verified kernel chains, never a disk-name heuristic.
    std::map<std::string, std::set<std::string>> vg_leaves;
    std::map<std::string, std::string> vg_labels;
    for (const auto& [name, node] : sys) {
        if (!node.dm || !node.uuid.starts_with("LVM-")) continue;
        const auto vg = node.uuid.substr(4, 32);
        const auto lv = node.uuid.size() >= 36 ? node.uuid.substr(36, 32) : "";
        const auto labels = dmNames(node.name);
        if (!lvmId(vg) || !lvmId(lv) || !labels ||
            (node.uuid.size() > 68 && (node.uuid[68] != '-' || !component(node.uuid.substr(69))))) {
            issue(Code::ConflictingEvidence, name, "Malformed active LVM UUID or escaped name");
            continue;
        }
        const auto vg_key = "lvm-vg:" + vg;
        const auto lv_key = "lvm-lv:" + node.uuid.substr(4);
        const auto [it, inserted] = vg_labels.emplace(vg_key, (*labels)[0]);
        if (!inserted && it->second != (*labels)[0])
            issue(Code::ConflictingEvidence, name, "One VG ID has conflicting names");
        auto [alias, fresh] = context.vg_names.emplace((*labels)[0], vg_key);
        if (!fresh && alias->second != vg_key) alias->second.clear();
        out.nodes.push_back({lv_key, OwnershipKind::LvmLv, {{"vg_id", vg}, {"lv_id", lv}, {"name", (*labels)[1]}}, true});
        out.edges.push_back({blockOwnershipKey(name), lv_key, "active-lv", "sysfs:dm/uuid"});
        out.edges.push_back({lv_key, vg_key, "in-vg", "sysfs:dm/uuid"});
        out.claims.push_back({blockOwnershipKey(name), OwnershipUse::Lvm, "sysfs:dm/uuid", "Active LVM volume"});
        std::set<std::string> active;
        std::function<void(const std::string&, std::size_t)> leaves = [&](const auto& current, std::size_t depth) {
            if (depth > 128 || active.contains(current) || !sys.contains(current)) {
                issue(Code::Cycle, name, "Incomplete or cyclic LVM backing chain");
                return;
            }
            if (current != name && !sys.at(current).uuid.starts_with("LVM-" + vg)) {
                vg_leaves[vg_key].insert(current);
                return;
            }
            if (sys.at(current).slaves.empty()) {
                issue(Code::MissingRelationship, name, "LVM layer has no observed backing extent");
                return;
            }
            active.insert(current);
            for (const auto& s : sys.at(current).slaves) leaves(s, depth + 1);
            active.erase(current);
        };
        leaves(name, 0);
    }
    std::set<std::string> active_pvs;
    for (const auto& [vg, label] : vg_labels) {
        out.nodes.push_back({vg, OwnershipKind::LvmVg, {{"name", label}, {"scope", "active kernel mappings only"}}, true});
        for (const auto& leaf : vg_leaves[vg]) {
            const auto pv = "lvm-pv:" + vg + ":" + leaf;
            active_pvs.insert(leaf);
            out.nodes.push_back({pv, OwnershipKind::LvmPv, {{"membership", "observed active backing extent"}}, true});
            out.edges.push_back({vg, pv, "active-pv", "sysfs"});
            out.edges.push_back({pv, blockOwnershipKey(leaf), "backed-by", "sysfs"});
        }
    }
    for (const auto& [name, b] : blocks) {
        if (b->signature.type == "LVM2_member" && !active_pvs.contains(name))
            issue(Code::IncompleteCoverage, name, "LVM member not covered by an active kernel mapping");
        if (b->signature.type == "linux_raid_member" && sys.at(name).holders.empty())
            issue(Code::IncompleteCoverage, name, "Inactive md membership is not resolved");
    }

    if (active_scan_profile) {
        auto& stage = active_scan_profile->stages["ownership.sysfs_lvm_md"];
        ++stage.calls;
        stage.milliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - topology_start).count();
    }
    const auto zfs_start = std::chrono::steady_clock::now();
    const auto zfs_module = list("/sys/module/zfs");
    const bool zfs_observed = std::any_of(inventory.mounts.begin(), inventory.mounts.end(),
        [](const auto& m) { return m.filesystem_type == "zfs"; }) ||
        std::any_of(inventory.nodes.begin(), inventory.nodes.end(),
        [](const auto& b) { return b.signature.type == "zfs_member"; });
    ZfsOwnershipRead zfs;
    if (zfs_module.absent && !zfs_observed) { zfs.complete = true; }
    else zfs = zfs_.read();
    out.coverage["zfs"] = zfs.complete;
    if (!zfs_module.absent && zfs.complete)
        context.zfs_control_number = text("/sys/class/misc/zfs/dev").value;
    if (!zfs.complete) context.issue(Code::MissingSource, "", "zfs", zfs.detail.empty() ? "Live pool membership unavailable" : zfs.detail);
    for (const auto& pool : zfs.pools) {
        const auto key = "zfs-pool:" + pool.guid;
        auto [it, inserted] = context.pools.emplace(pool.name, key);
        if (!inserted) {
            it->second.clear();
            context.issue(Code::ConflictingEvidence, key, "zfs", "Duplicate pool name");
        }
        out.nodes.push_back({key, OwnershipKind::ZfsPool, {{"name", pool.name}, {"guid", pool.guid}}, pool.complete && !pool.guid.empty()});
        out.claims.push_back({key, OwnershipUse::Zfs, "zfs:kernel-config", "Imported pool " + pool.name});
        std::size_t count = 0;
        std::function<void(const ZfsVdevObservation&, const std::string&, std::size_t)> add =
            [&](const auto& v, const auto& parent, std::size_t depth) {
                if (++count > 8192 || depth > 64) {
                    context.issue(Code::Cycle, key, "zfs", "Vdev tree exceeds bounds"); return;
                }
                const auto vk = key + ":vdev:" + v.key;
                out.nodes.push_back({vk, OwnershipKind::ZfsVdev, {{"type", v.type}}, v.complete});
                out.edges.push_back({parent, vk, "vdev", "zfs:kernel-config"});
                if (!v.children.empty()) {
                    if (v.type != "root" && v.type != "mirror" && v.type != "raidz" &&
                        v.type != "replacing" && v.type != "spare" && v.type != "draid")
                        context.issue(Code::UnsupportedTopology, vk, "zfs", "Unsupported vdev grouping");
                    for (const auto& child : v.children) add(child, vk, depth + 1);
                } else if (v.path && (v.type == "disk" || v.type == "file")) {
                    if (const auto owner = context.pathOwner(*v.path))
                        out.edges.push_back({vk, *owner, "backed-by", "zfs:kernel-config"});
                    else context.issue(Code::MissingRelationship, vk, "zfs", "Unresolved vdev path");
                } else context.issue(Code::UnsupportedTopology, vk, "zfs", "Missing or unsupported vdev");
            };
        add(pool.root, key, 0);
    }
    // Zvol symlinks are metadata aliases. A verified virtual block devnum
    // connects a dataset consumer to its imported pool, without reading it.
    for (const auto& [pool_name, pool_key] : context.pools) {
        if (pool_key.empty() || !component(pool_name)) continue;
        const auto directory = "/dev/zvol/" + pool_name;
        std::size_t entries = 0;
        std::function<void(const std::string&, std::size_t)> volumes = [&](const auto& path, std::size_t depth) {
            if (depth > 64 || ++entries > 16384) {
                context.issue(Code::IncompleteCoverage, pool_key, "zvol", "Zvol traversal limit"); return;
            }
            const auto contents = list(path);
            if (!contents.value) {
                if (!contents.absent) context.issue(Code::MissingSource, pool_key, "zvol", "Zvol directory unreadable");
                return;
            }
            for (const auto& entry : *contents.value) {
                if (!component(entry)) {
                    context.issue(Code::UnsupportedTopology, pool_key, "zvol", "Unsupported dataset component"); continue;
                }
                const auto alias = path + "/" + entry;
                const auto before = io_.locate(alias), after = io_.locate(alias);
                if (!before.value || before.value != after.value) {
                    context.issue(Code::SnapshotChanged, pool_key, "zvol", "Zvol metadata changed or unavailable"); continue;
                }
                if (!before.value->block) { volumes(alias, depth + 1); continue; }
                std::vector<std::string> matches;
                for (const auto& [name, b] : blocks)
                    if (b->observation.device_number == before.value->device &&
                        b->observation.observation_kind == BlockObservationKind::VirtualOrPseudo)
                        matches.push_back(name);
                if (matches.size() == 1)
                    out.edges.push_back({blockOwnershipKey(matches.front()), pool_key, "zvol-in-pool", "zvol:stat"});
                else context.issue(Code::MissingRelationship, pool_key, "zvol", "No unique virtual zvol block node");
            }
        };
        volumes(directory, 0);
    }
    for (const auto& m : inventory.mounts) {
        if (m.filesystem_type != "zfs") continue;
        const auto name = m.source.substr(0, m.source.find('/'));
        if (!context.pools.contains(name) || context.pools.at(name).empty()) continue;
        const auto use = m.mount_point == "/" ? OwnershipUse::Root :
            (m.mount_point == "/boot" || m.mount_point.starts_with("/boot/")) ? OwnershipUse::Boot :
            (m.mount_point == "/home" || m.mount_point.starts_with("/home/")) ? OwnershipUse::Home : OwnershipUse::Mounted;
        out.claims.push_back({context.pools.at(name), use, "mountinfo:" + std::to_string(m.mount_id), m.mount_point});
    }
    for (const auto& [name, b] : blocks)
        if (b->signature.type == "zfs_member" &&
            std::none_of(out.edges.begin(), out.edges.end(), [&](const auto& e) {
                return e.source == "zfs:kernel-config" && e.provider == blockOwnershipKey(name);
            }))
            context.issue(Code::IncompleteCoverage, blockOwnershipKey(name), "zfs", "Unimported or unresolved ZFS member");

    if (active_scan_profile) {
        auto& stage = active_scan_profile->stages["ownership.zfs"];
        ++stage.calls;
        stage.milliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - zfs_start).count();
    }
    { ScanStage stage("ownership.proxmox"); readProxmoxOwnership(context); }
    { ScanStage stage("ownership.process_before"); readProcessOwnership(context); }
    // Probes finish before the final topology recheck; their own block fds
    // must not appear in the process scan.
    if (fresh_) {
        { ScanStage stage("ownership.fresh_signatures"); out.fresh_signatures = fresh_->read(inventory); }
        const bool processes_before = out.coverage.at("processes");
        { ScanStage stage("ownership.process_after"); readProcessOwnership(context); } // Required recheck.
        out.coverage["processes"] = processes_before && out.coverage.at("processes");
        out.coverage["inactive_membership"] = true;
        std::erase_if(out.issues, [](const auto& i) { return i.source == "inactive_membership"; });
        for (const auto& b : inventory.nodes) {
            if (b.observation.observation_kind == BlockObservationKind::VirtualOrPseudo) continue;
            const auto key = blockOwnershipKey(b.observation.kernel_name);
            if (!out.fresh_signatures.contains(key) || !freshSignatureComplete(out.fresh_signatures.at(key)))
                out.coverage["inactive_membership"] = false;
        }
    }
    ScanStage recheck("ownership.final_revalidation");
    if (context.zfs_control_observed) {
        const auto final_zfs = zfs_.read();
        if (!final_zfs.complete || zfs.pools != final_zfs.pools) {
            out.coverage["zfs"] = false;
            context.issue(Code::SnapshotChanged,"","zfs",
                "ZFS control ownership capture changed or became incomplete around process observations");
        }
    }
    // Repeat every acquired topology fact after the slower configuration reads.
    bool topology_changed = false;
    for (const auto& [path, before] : texts) {
        const auto after = io_.text(path);
        if (before.value != after.value || before.absent != after.absent) {
            topology_changed = true;
            issue(Code::SnapshotChanged, "", "Topology text changed: " + path);
        }
    }
    for (const auto& [path, before] : lists) {
        auto after = io_.list(path);
        if (after.value) std::sort(after.value->begin(), after.value->end());
        if (before.value != after.value || before.absent != after.absent) {
            topology_changed = true;
            issue(Code::SnapshotChanged, "", "Topology listing changed: " + path);
        }
    }
    if (topology_changed)
        for (const auto& [name, b] : blocks) {
            (void)b;
            issue(Code::SnapshotChanged, name, "Topology epoch changed during ownership scan");
        }
    return out;
}
} // namespace drivelab
