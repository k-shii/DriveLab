#include "platform/linux/linux_ownership_source.h"
#include "core/scan_profile.h"
#include "core/scan_control.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <tuple>
#include <unordered_map>
#include <map>
#include <set>
#include <sstream>

namespace drivelab {
namespace {
using Code = OwnershipReasonCode;
std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string part;
    while (std::getline(in, part, delimiter)) out.push_back(trim(part));
    return out;
}
bool numeric(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
bool diskKey(const std::string& key) {
    if (key == "rootfs" || key == "efidisk0" || key == "tpmstate0" || key == "vmstate") return true;
    for (const auto* prefix : {"scsi", "sata", "ide", "virtio", "unused", "mp", "dev"})
        if (key.starts_with(prefix) && numeric(key.substr(std::string(prefix).size()))) return true;
    return false;
}
bool nonStorageAnonymousHandle(const std::string& target) {
    if (!target.starts_with("anon_inode:")) return false;
    auto type = target.substr(11);
    if (type.starts_with('[') && type.ends_with(']')) type = type.substr(1,type.size()-2);
    // anon_inode is not an absence-of-storage guarantee: io_uring can retain
    // registered block files; fanotify can retain filesystem/event references.
    // Only reviewed object types qualify, with no suffix/prefix matching.
    static const std::set<std::string> types{
        "eventfd", "eventpoll", "signalfd", "timerfd", "inotify", "pidfd", "bpf-prog"
    };
    return types.contains(type);
}
struct Storage { std::string type, id; std::map<std::string, std::string> properties; bool valid = true; };
}
void readProxmoxOwnership(OwnershipScanContext& c) {
    c.evidence.coverage["proxmox"] = true;
    const auto pve = c.io.list("/etc/pve");
    if (pve.absent) return;
    auto problem = [&](const std::string& node, const std::string& source, const std::string& detail) {
        c.evidence.coverage["proxmox"] = false;
        c.issue(Code::IncompleteCoverage, node, source, detail);
    };
    if (!pve.value) { problem("", "/etc/pve", "Cluster configuration unreadable"); return; }
    const auto local = c.io.canonical("/etc/pve/local");
    if (!local.value || !local.value->starts_with("/etc/pve/nodes/") ||
        local.value->substr(15).find('/') != std::string::npos || local.value->size() <= 15) {
        problem("", "/etc/pve/local", "Cannot establish the local Proxmox node"); return;
    }
    const auto local_name = local.value->substr(15);
    std::map<std::string, OwnershipRead<std::string>> texts, links;
    std::map<std::string, OwnershipRead<std::vector<std::string>>> listings;
    auto read = [&](const std::string& path) { auto r = c.io.text(path); texts[path] = r; return r; };
    auto link = [&](const std::string& path) { auto r = c.io.canonical(path); links[path] = r; return r; };
    auto list = [&](const std::string& path) {
        auto r = c.io.list(path);
        if (r.value) std::sort(r.value->begin(), r.value->end());
        listings[path] = r; return r;
    };
    const auto config = read("/etc/pve/storage.cfg");
    std::vector<Storage> entries;
    if (!config.value) problem("", "/etc/pve/storage.cfg", "Storage configuration unavailable");
    else {
        std::istringstream input(*config.value);
        std::string line;
        Storage* current = nullptr;
        while (std::getline(input, line)) {
            const auto value = trim(line);
            if (value.empty() || value.starts_with('#')) continue;
            if (!line.empty() && !std::isspace(static_cast<unsigned char>(line.front()))) {
                const auto colon = value.find(':');
                if (colon == std::string::npos || value.find(':', colon + 1) != std::string::npos) {
                    problem("", "storage.cfg", "Malformed storage section"); current = nullptr; continue;
                }
                entries.push_back({trim(value.substr(0, colon)), trim(value.substr(colon + 1)), {}, true});
                current = &entries.back();
                if (current->id.empty() || current->id.find_first_of(" \t/") != std::string::npos) current->valid = false;
            } else if (current) {
                const auto space = value.find_first_of(" \t");
                if (space == std::string::npos ||
                    !current->properties.emplace(value.substr(0, space), trim(value.substr(space + 1))).second)
                    current->valid = false;
            } else problem("", "storage.cfg", "Storage property outside a section");
        }
    }
    std::map<std::string, std::string> storage_keys;
    for (const auto& entry : entries) {
        if (const auto n = entry.properties.find("nodes"); n != entry.properties.end()) {
            const auto names = split(n->second, ',');
            if (std::find(names.begin(), names.end(), local_name) == names.end()) continue;
        }
        const auto key = "pve-storage:" + entry.id;
        if (!storage_keys.emplace(entry.id, key).second) problem(key, "storage.cfg", "Duplicate local storage ID");
        c.evidence.nodes.push_back({key, OwnershipKind::ProxmoxStorage, {{"type", entry.type}, {"id", entry.id}}, entry.valid});
        c.evidence.claims.push_back({key, OwnershipUse::ConfiguredStorage, "storage.cfg", "Configured local storage"});
        auto property = [&](const std::string& name) {
            const auto p = entry.properties.find(name);
            return p == entry.properties.end() ? std::string{} : p->second;
        };
        std::optional<std::string> owner;
        if (entry.type == "dir") {
            const auto path = property("path");
            if (path.starts_with('/')) owner = c.pathOwner(path);
        } else if (entry.type == "lvm" || entry.type == "lvmthin") {
            const auto vg = property("vgname");
            if (c.vg_names.contains(vg) && !c.vg_names.at(vg).empty()) owner = c.vg_names.at(vg);
        } else if (entry.type == "zfspool") {
            const auto pool = property("pool");
            const auto name = pool.substr(0, pool.find('/'));
            if (c.pools.contains(name) && !c.pools.at(name).empty()) owner = c.pools.at(name);
        }
        if (owner) c.evidence.edges.push_back({key, *owner, "configured-on", "storage.cfg"});
        else problem(key, "storage.cfg", "Unsupported or unresolved local storage backend");
        if (!entry.valid) problem(key, "storage.cfg", "Malformed or conflicting storage configuration");
    }
    for (const auto* kind : {"qemu-server", "lxc"}) {
        const auto directory = *local.value + "/" + kind;
        const auto files = list(directory);
        if (!files.value) { problem("", directory, "Local guest directory unavailable"); continue; }
        for (const auto& file : *files.value) {
            if (!file.ends_with(".conf")) continue;
            const auto id = file.substr(0, file.size() - 5);
            if (!numeric(id)) { problem("", directory, "Malformed guest configuration filename"); continue; }
            const auto source = directory + "/" + file;
            const auto text = read(source);
            const auto key = "pve-guest:" + std::string(kind) + ":" + id;
            c.evidence.nodes.push_back({key, OwnershipKind::ProxmoxGuest, {{"kind", kind}, {"id", id}}, text.value.has_value()});
            bool has_storage = false;
            if (!text.value) { problem(key, source, "Guest configuration unreadable"); continue; }
            std::istringstream input(*text.value);
            std::string line;
            while (std::getline(input, line)) {
                const auto value = trim(line);
                if (value.empty() || value.starts_with('#') || value.starts_with('[')) continue;
                const auto colon = value.find(':');
                if (colon == std::string::npos) { problem(key, source, "Malformed guest property"); continue; }
                const auto name = trim(value.substr(0, colon)), data = trim(value.substr(colon + 1));
                if (name == "args" || name == "hookscript" || name.starts_with("lxc.mount") ||
                    name.starts_with("lxc.cgroup") || name.starts_with("lxc.hook")) {
                    problem(key, source, "Custom guest ownership mechanism is not resolvable"); has_storage = true; continue;
                }
                if (name.starts_with("hostpci") && numeric(name.substr(7))) {
                    has_storage = true;
                    const auto field = data.substr(0, data.find(','));
                    const auto devices = split(field, ';');
                    for (auto pci : devices) {
                        if (pci.starts_with("host=")) pci.erase(0, 5);
                        if (pci.size() == 5 || pci.size() == 7) pci = "0000:" + pci;
                        const bool valid = (pci.size() == 10 || pci.size() == 12) &&
                            pci[4] == ':' && pci[7] == ':' &&
                            (pci.size() == 10 || pci[10] == '.') &&
                            std::all_of(pci.begin(), pci.end(), [](unsigned char x) {
                                return std::isxdigit(x) || x == ':' || x == '.';
                            });
                        bool found = false;
                        if (valid) {
                            const auto functions = list("/sys/bus/pci/devices");
                            if (functions.value) for (const auto& function : *functions.value) {
                                if (function != pci && !(pci.size() == 10 && function.starts_with(pci + "."))) continue;
                                const auto path = link("/sys/bus/pci/devices/" + function);
                                if (!path.value) continue;
                                for (const auto& b : c.inventory.nodes) {
                                    if (b.observation.sysfs_path && b.observation.sysfs_path->starts_with(*path.value + "/")) {
                                        found = true;
                                        c.evidence.edges.push_back({key, blockOwnershipKey(b.observation.kernel_name), "pci-passthrough", source + ":" + name});
                                    }
                                }
                            }
                        }
                        if (!found) problem(key, source + ":" + name, "PCI assignment has no trustworthy visible storage owner");
                    }
                } else if (diskKey(name)) {
                    auto volume = data.substr(0, data.find(','));
                    if (volume.starts_with("file=")) volume.erase(0, 5);
                    if (volume == "none" || volume == "cdrom") continue;
                    has_storage = true;
                    std::optional<std::string> owner;
                    if (volume.starts_with('/')) owner = c.pathOwner(volume);
                    else {
                        const auto separator = volume.find(':');
                        if (separator != std::string::npos) {
                            const auto storage = storage_keys.find(volume.substr(0, separator));
                            if (storage != storage_keys.end()) owner = storage->second;
                        }
                    }
                    if (owner) c.evidence.edges.push_back({key, *owner, "guest-assignment", source + ":" + name});
                    else problem(key, source + ":" + name, "Unresolved raw disk, volume, or bind mount");
                }
            }
            if (has_storage) c.evidence.claims.push_back({key, OwnershipUse::Guest, source, "Configured guest ownership, including stopped guests and snapshots"});
            else {
                // A diskless guest is not an incomplete storage consumer.
                c.evidence.nodes.pop_back();
            }
        }
    }
    bool changed = false;
    for (const auto& [path, before] : texts) {
        const auto after = c.io.text(path);
        if (before.value != after.value || before.absent != after.absent) changed = true;
    }
    for (const auto& [path, before] : listings) {
        auto after = c.io.list(path);
        if (after.value) std::sort(after.value->begin(), after.value->end());
        if (before.value != after.value || before.absent != after.absent) changed = true;
    }
    for (const auto& [path, before] : links) {
        const auto after = c.io.canonical(path);
        if (before.value != after.value || before.absent != after.absent) changed = true;
    }
    const auto after_local = c.io.canonical("/etc/pve/local");
    if (after_local.value != local.value) changed = true;
    if (changed) {
        problem("", "proxmox", "Configuration changed during scan");
        for (const auto& n : c.evidence.nodes)
            if (n.kind == OwnershipKind::ProxmoxStorage || n.kind == OwnershipKind::ProxmoxGuest)
                c.issue(Code::SnapshotChanged, n.key, "proxmox", "Configuration epoch changed");
    }
}

namespace {
void readProcessOwnershipPass(OwnershipScanContext& c,
                              std::vector<OwnershipReason>& acquisition_gaps) {
    checkScanCancelled();
    ScanStage timing("ownership.process_pass");
    c.evidence.coverage_gaps.try_emplace("processes");
    const auto namespace_start = c.evidence.namespace_mounts.size();
    auto problem = [&](const std::string& detail, const std::string& owner = "") {
        c.evidence.coverage_gaps["processes"].push_back({Code::IncompleteCoverage,owner,"proc",detail});
    };
    // Failed collection is retryable; observed semantic uncertainty is not.
    // Keep this category explicit at each call site, never inferred from text.
    auto acquisitionProblem = [&](const std::string& detail) {
        acquisition_gaps.push_back({Code::IncompleteCoverage,"","proc",detail});
    };
    auto deviceText = [](const BlockDeviceNumber& dev) {
        return std::to_string(dev.major_number) + ":" + std::to_string(dev.minor_number);
    };
    // Index immutable inventory, not host reads. Duplicate aliases still form
    // the same set of possible owners as the original exhaustive traversal.
    using DeviceKey = std::pair<std::uint32_t, std::uint32_t>;
    auto deviceKey = [](const BlockDeviceNumber& dev) {
        return DeviceKey{dev.major_number, dev.minor_number};
    };
    std::map<DeviceKey, std::set<std::string>> block_owners;
    for (const auto& b : c.inventory.nodes) if (b.observation.device_number)
        block_owners[deviceKey(*b.observation.device_number)].insert(blockOwnershipKey(b.observation.kernel_name));
    auto blockOwners = [&](const BlockDeviceNumber& dev) {
        const auto found = block_owners.find(deviceKey(dev));
        return found == block_owners.end() ? std::set<std::string>{} : found->second;
    };
    // Keep all retained passes and their original order. An index merely avoids
    // visiting mounts whose device number cannot match this file observation.
    std::map<DeviceKey, std::vector<std::size_t>> mounts_by_device;
    for (std::size_t i = 0; i < c.evidence.namespace_mounts.size(); ++i)
        mounts_by_device[deviceKey(c.evidence.namespace_mounts[i].mount.device_number)].push_back(i);
    std::map<std::string, std::pair<std::string, std::string>> namespaces;
    auto inspectNamespace = [&](const std::string& ns, const std::string& path) {
        if (!ns.starts_with("mnt:[") || !ns.ends_with("]") ||
            !numeric(ns.substr(5, ns.size() - 6))) { problem("Malformed mount namespace"); return; }
        if (namespaces.contains(ns)) return;
        if (namespaces.size() >= 256) { problem("Mount namespace scan limit"); return; }
        const auto input = c.io.text(path);
        namespaces[ns] = {path, input.value.value_or("")};
        if (!input.value) { acquisitionProblem("Unreadable mount namespace " + ns); return; }
        if (input.value->size() > 4 * 1024 * 1024) {
            problem("Oversized mount namespace " + ns); return;
        }
        ResolvedInventorySnapshot parsed;
        std::istringstream lines(*input.value); std::string line;
        std::set<std::uint64_t> mount_ids; std::size_t count = 0;
        // An empty view can hide mounts outside the task's root. Neither it
        // nor a truncated record set establishes absence of target ownership.
        if (input.value->empty())
            problem("Empty mount namespace " + ns + " path=" + path + " bytes=0");
        else if (input.value->back() != '\n')
            problem("Truncated mount namespace " + ns + " path=" + path +
                    " bytes=" + std::to_string(input.value->size()));
        while (std::getline(lines,line)) {
            if (lines.eof()) break; // Never scope a truncated tail as a complete observation.
            if (++count > 65536) { problem("Mount namespace record limit " + ns); break; }
            ResolvedInventorySnapshot row;
            parseMountInfo(line + "\n",row);
            if (!row.issues.empty()) {
                // A malformed non-address field must not discard all the other
                // mounts. Scope only a fully parsed numeric kernel record address.
                std::istringstream fields(line); std::string id,parent,dev;
                BlockDeviceNumber number;
                auto decimal = [](const std::string& text, auto& value) {
                    const auto r = std::from_chars(text.data(),text.data()+text.size(),value);
                    return !text.empty() && r.ec == std::errc{} && r.ptr == text.data()+text.size();
                };
                std::uint64_t mount_id=0,parent_id=0;
                fields >> id >> parent >> dev; const auto colon=dev.find(':');
                std::set<std::string> owners;
                if (decimal(id,mount_id) && mount_id && decimal(parent,parent_id) && colon!=std::string::npos &&
                    decimal(dev.substr(0,colon),number.major_number) && decimal(dev.substr(colon+1),number.minor_number))
                    owners = blockOwners(number);
                const auto detail = "Malformed mount in namespace " + ns + " record=" + line;
                if (owners.empty()) problem(detail);
                else for (const auto& owner : owners) problem(detail,owner);
                continue;
            }
            for (auto& mount : row.mounts) {
                if (!mount_ids.insert(mount.mount_id).second) problem("Duplicate mount ID in namespace " + ns);
                parsed.mounts.push_back(std::move(mount));
            }
        }
        for (auto& mount : parsed.mounts) {
            std::vector<std::string> owners;
            for (const auto& b : c.inventory.nodes)
                if (b.observation.device_number == mount.device_number)
                    owners.push_back(blockOwnershipKey(b.observation.kernel_name));
            const auto use = mount.mount_point == "/" ? OwnershipUse::Root :
                (mount.mount_point == "/boot" || mount.mount_point.starts_with("/boot/")) ? OwnershipUse::Boot :
                (mount.mount_point == "/home" || mount.mount_point.starts_with("/home/")) ? OwnershipUse::Home : OwnershipUse::Mounted;
            std::string owner;
            if (owners.size() == 1) {
                owner = owners.front(); mount.node = owner.substr(6);
            } else if (owners.empty() && mount.filesystem_type == "zfs") {
                const auto pool = c.pools.find(mount.source.substr(0, mount.source.find('/')));
                if (pool != c.pools.end()) owner = pool->second;
            }
            if (!owner.empty())
                c.evidence.claims.push_back({owner, use, "proc-mountinfo:" + ns + ":" + std::to_string(mount.mount_id), mount.mount_point});
            else if (!owners.empty() || !harmlessOwnershipMount(mount)) {
                const auto detail = "Unattributed mount in namespace " + ns + " dev=" +
                    deviceText(mount.device_number) + " type=" + mount.filesystem_type +
                    " source=" + mount.source + " target=" + mount.mount_point;
                if (owners.empty()) problem(detail);
                else for (const auto& candidate : owners) problem(detail,candidate);
            }
            mounts_by_device[deviceKey(mount.device_number)].push_back(c.evidence.namespace_mounts.size());
            c.evidence.namespace_mounts.push_back({ns, std::move(mount)});
        }
    };
    // pmxcfs is a virtual configuration interface with persistent backing.
    // Bind it to the actual scanner's host mount and database, never just a
    // /dev/fuse pathname or a process's root disk.
    std::map<std::string, std::set<std::string>> pmxcfs_backings;
    std::map<std::string, OwnershipPath> pmxcfs_paths;
    std::optional<std::string> pmxcfs_local;
    auto inspectPmxcfs = [&] {
        std::set<std::string> devices;
        for (const auto& entry : c.evidence.namespace_mounts)
            if (pmxcfsOwnershipMount(entry.mount)) devices.insert(deviceText(entry.mount.device_number));
        for (const auto& mount : c.inventory.mounts)
            if (pmxcfsOwnershipMount(mount)) devices.insert(deviceText(mount.device_number));
        if (devices.empty()) return;
        const auto self_ns = c.io.canonical("/proc/self/ns/mnt");
        const auto mounted = c.io.locate("/etc/pve");
        const auto local = c.io.canonical("/etc/pve/local");
        const auto database = c.io.locate("/var/lib/pve-cluster/config.db");
        bool identified = self_ns.value && mounted.value && !mounted.value->block &&
            mounted.value->filesystem_object && local.value &&
            local.value->starts_with("/etc/pve/nodes/") && local.value->size() > 15 &&
            local.value->substr(15).find('/') == std::string::npos;
        std::size_t matches = 0;
        if (identified) for (auto i=namespace_start;i<c.evidence.namespace_mounts.size();++i) {
            const auto& entry = c.evidence.namespace_mounts[i];
            if (entry.namespace_id == *self_ns.value && entry.mount.mount_point == "/etc/pve") {
                ++matches;
                if (!pmxcfsOwnershipMount(entry.mount) || entry.mount.device_number != mounted.value->device)
                    identified = false;
            }
        }
        identified = identified && matches == 1;
        std::set<std::string> owners;
        if (identified && database.value && !database.value->block &&
            database.value->filesystem_object && !database.value->known_nonstorage) {
            owners = blockOwners(database.value->device);
            // A ZFS filesystem may have no block dev_t; use the already observed pool.
            if (owners.empty()) for (const auto& entry : c.evidence.namespace_mounts) {
                if (entry.mount.device_number != database.value->device || entry.mount.filesystem_type != "zfs") continue;
                const auto pool = c.pools.find(entry.mount.source.substr(0,entry.mount.source.find('/')));
                if (pool != c.pools.end() && !pool->second.empty()) owners.insert(pool->second);
            }
        }
        if (!identified || owners.size() != 1) {
            problem("pmxcfs identity or persistent database backing unavailable/ambiguous: /var/lib/pve-cluster/config.db");
            return;
        }
        pmxcfs_paths["/etc/pve"] = *mounted.value;
        pmxcfs_paths["/var/lib/pve-cluster/config.db"] = *database.value;
        pmxcfs_local = *local.value;
        const auto mounted_device = deviceText(mounted.value->device);
        for (const auto& dev : devices) {
            if (dev != mounted_device) {
                problem("pmxcfs instance has no inspected persistent backing: dev=" + dev);
                continue;
            }
            pmxcfs_backings[dev] = owners;
        }
        c.evidence.claims.push_back({*owners.begin(),OwnershipUse::ConfiguredStorage,
            "pmxcfs:database","Active pmxcfs backing: /var/lib/pve-cluster/config.db"});
    };
    std::set<std::string> namespace_handles;
    auto fileOwner = [&](const OwnershipPath& info, const std::string& target,
                         const std::string& source, bool uncertain = false) {
        ScanStage timing("ownership.process_file_attribution");
        if (!uncertain && c.accountZfsControl(info)) return;
        if (!uncertain && c.accountSocket(info,source)) return;
        bool memory = info.known_nonstorage || target.starts_with("pipe:[") ||
            target.starts_with("/memfd:") || target.starts_with("memfd:") ||
            (!uncertain && !info.block && info.device.major_number == 0 && nonStorageAnonymousHandle(target));
        const bool opaque = !info.block && !memory &&
            (!info.filesystem_object || target.starts_with("anon_inode:") || target.starts_with("mnt:["));
        auto owners = info.block || !opaque ? blockOwners(info.device) : std::set<std::string>{};
        bool unknown_filesystem = false;
        if (!info.block && !opaque) for (const auto index : mounts_by_device[deviceKey(info.device)]) {
            const auto& entry = c.evidence.namespace_mounts[index];
            if (pmxcfsOwnershipMount(entry.mount)) {
                const auto backing = pmxcfs_backings.find(deviceText(info.device));
                if (backing != pmxcfs_backings.end()) owners.insert(backing->second.begin(),backing->second.end());
                else unknown_filesystem = true;
            } else if (harmlessOwnershipMount(entry.mount)) memory = true;
            else if (entry.mount.filesystem_type == "zfs") {
                const auto pool = c.pools.find(entry.mount.source.substr(0, entry.mount.source.find('/')));
                if (pool != c.pools.end() && !pool->second.empty()) owners.insert(pool->second);
                else unknown_filesystem = true;
            } else if (owners.empty()) unknown_filesystem = true;
        }
        const auto detail = source + " dev=" + deviceText(info.device) +
            (info.character_device ? " char=" + deviceText(*info.character_device) : "") + " target=" + target;
        if (owners.size() == 1 && !unknown_filesystem) {
            c.evidence.claims.push_back({*owners.begin(),info.block ? OwnershipUse::OpenBlockHandle :
                OwnershipUse::OpenFileHandle,source,target});
            if (uncertain) problem("Incomplete file observation: " + detail,*owners.begin());
        } else if (!owners.empty() && !unknown_filesystem) {
            for (const auto& owner : owners) problem("Ambiguous file owner: " + detail,owner);
        } else if (info.block || opaque || unknown_filesystem || !memory)
            problem("Unattributed open/mapped file or opaque kernel handle: " + detail);
    };
    using Mapping = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::string>;
    std::unordered_map<std::string, std::set<Mapping>> parsed_maps;
    std::size_t parsed_map_bytes = 0;
    auto mappings = [&](const std::string& base) {
        ScanStage timing("ownership.process_maps");
        std::set<Mapping> result;
        const auto input = c.io.text(base + "/maps");
        if (!input.value) { acquisitionProblem("Unreadable process mappings: " + base); return result; }
        if (input.value->size() > 4 * 1024 * 1024) {
            problem("Oversized process mappings: " + base); return result;
        }
        if (!input.value->empty() && input.value->back() != '\n') problem("Truncated process mappings: " + base);
        // Still read every task and both bracket observations. Only reuse the
        // pure parse of byte-identical, complete, valid text within this pass.
        if (const auto cached = parsed_maps.find(*input.value); cached != parsed_maps.end()) {
            scanCount("process.maps_parse_cache_hits");
            return cached->second;
        }
        bool cacheable = input.value->empty() || input.value->back() == '\n';
        auto number = [](const std::string& text, auto& out, int radix) {
            const auto r = std::from_chars(text.data(), text.data() + text.size(), out, radix);
            return !text.empty() && r.ec == std::errc{} && r.ptr == text.data() + text.size();
        };
        std::istringstream lines(*input.value); std::string line; std::size_t count = 0;
        while (std::getline(lines, line)) {
            if (++count > 65536) { cacheable = false; problem("Process mapping limit"); break; }
            std::istringstream fields(line);
            std::string range, permissions, offset, dev, inode, path;
            std::uint64_t start = 0, end = 0, off = 0, ino = 0;
            std::uint32_t major = 0, minor = 0;
            if (!(fields >> range >> permissions >> offset >> dev >> inode)) {
                cacheable = false;
                problem("Malformed process mapping"); continue;
            }
            const auto dash = range.find('-'), colon = dev.find(':');
            const bool known_device = colon != std::string::npos && number(dev.substr(0,colon),major,16) &&
                number(dev.substr(colon+1),minor,16);
            if (dash == std::string::npos || !known_device ||
                !number(range.substr(0,dash),start,16) || !number(range.substr(dash+1),end,16) || start >= end ||
                !number(offset,off,16) || !number(inode,ino,10) || permissions.size() != 4) {
                cacheable = false;
                const auto owners = known_device ? blockOwners({major,minor}) : std::set<std::string>{};
                const auto detail = "Malformed process mapping: " + base + " record=" + line;
                if (owners.empty()) problem(detail);
                else for (const auto& owner : owners) problem(detail,owner);
                continue;
            }
            std::getline(fields, path); path = trim(path);
            // Anonymous mappings have no backing inode. Ignore address/heap churn;
            // retain and recheck the set of file backings, including deleted files.
            if (ino) result.emplace(major,minor,ino,path);
        }
        if (cacheable && input.value->size() <= 8 * 1024 * 1024 - parsed_map_bytes) {
            parsed_map_bytes += input.value->size();
            parsed_maps.emplace(*input.value, result);
        }
        return result;
    };
    const auto self = c.io.canonical("/proc/self/ns/mnt");
    if (self.value) inspectNamespace(*self.value, "/proc/self/mountinfo");
    // Only the procfs instance used for enumeration controls our PID visibility.
    // subset=pid hides non-task entries; actual metadata/swaps reads stay required.
    const auto proc_before = c.io.locate("/proc");
    std::vector<const MountEvidence*> scan_proc;
    if (self.value && proc_before.value && !proc_before.value->block)
        for (auto i=namespace_start;i<c.evidence.namespace_mounts.size();++i) {
            const auto& entry=c.evidence.namespace_mounts[i];
            if (entry.namespace_id == *self.value && entry.mount.mount_point == "/proc" &&
                entry.mount.device_number == proc_before.value->device) scan_proc.push_back(&entry.mount);
        }
    if (scan_proc.size() != 1 || scan_proc.front()->filesystem_type != "proc" || scan_proc.front()->root != "/")
        problem("Cannot establish the procfs instance used for enumeration");
    else {
        auto restricted = [](const std::string& options) {
            std::istringstream fields(options); std::string field;
            while (std::getline(fields,field,','))
                if (field.starts_with("hidepid=") && field != "hidepid=0" && field != "hidepid=off") return true;
            return false;
        };
        const auto& mount = *scan_proc.front();
        if (restricted(mount.options) || restricted(mount.super_options))
            problem("Restricted procfs visibility: scanned /proc dev=" + deviceText(mount.device_number) +
                " options=" + mount.options + " super_options=" + mount.super_options);
    }
    if (self.value) for (auto i=namespace_start;i<c.evidence.namespace_mounts.size();++i) {
        const auto& entry=c.evidence.namespace_mounts[i];
        if (entry.namespace_id != *self.value || !entry.mount.mount_point.starts_with("/proc/")) continue;
        const auto relative=entry.mount.mount_point.substr(6);
        const auto first=relative.substr(0,relative.find('/'));
        if (numeric(first) || first=="self" || first=="thread-self" || first=="swaps")
            problem("Overmounted proc ownership evidence: " + entry.mount.mount_point);
    }
    const auto pids = c.io.list("/proc");
    if (!self.value || !pids.value) { acquisitionProblem("Process or namespace enumeration unavailable"); }
    else {
        std::size_t handles = 0;
        std::set<std::string> ordered_pids(pids.value->begin(), pids.value->end());
        std::map<std::string, std::set<std::string>> task_sets;
        std::vector<std::pair<std::string,std::string>> tasks;
        for (const auto& pid : ordered_pids) {
            if (!numeric(pid)) continue;
            const auto path = "/proc/" + pid + "/task";
            const auto tids = c.io.list(path);
            if (!tids.value || tids.value->empty()) { acquisitionProblem("Thread enumeration unavailable at PID " + pid); continue; }
            task_sets[path] = std::set<std::string>(tids.value->begin(), tids.value->end());
            for (const auto& tid : task_sets[path]) {
                if (!numeric(tid) || tasks.size() >= 32768) { problem("Malformed/excessive thread enumeration"); break; }
                tasks.emplace_back(pid + ":" + tid, tid == pid ? "/proc/" + pid : path + "/" + tid);
            }
            if (tasks.size() >= 32768) break;
        }
        scanCount("process.tasks", tasks.size());
        // Collect namespace mount devices before attributing any file backing.
        for (const auto& task : tasks) {
            const auto ns = c.io.canonical(task.second + "/ns/mnt");
            if (ns.value) inspectNamespace(*ns.value, task.second + "/mountinfo");
        }
        inspectPmxcfs();
        for (const auto& [pid, base] : tasks) {
            const auto ns = c.io.canonical(base + "/ns/mnt");
            if (!ns.value) acquisitionProblem("Uninspected mount namespace at PID " + pid);
            else inspectNamespace(*ns.value, base + "/mountinfo");
            const auto mapped = mappings(base);
            for (const auto& [major, minor, inode, path] : mapped) {
                (void)inode;
                fileOwner(OwnershipPath{false,{major,minor}, true, false, std::nullopt, false}, path, "proc:maps:" + pid);
            }
            const auto fds = c.io.list(base + "/fd");
            if (!fds.value) { acquisitionProblem("File descriptor enumeration incomplete at PID " + pid); continue; }
            for (const auto* reference : {"cwd", "root"}) {
                const auto path = base + "/" + reference;
                const auto before = c.io.locate(path), after = c.io.locate(path);
                if (before.value && after.value && before.value != after.value) {
                    fileOwner(*before.value,path,path,true);
                    fileOwner(*after.value,path,path,true);
                } else if (!before.value || before.value != after.value) {
                    // Kernel threads can have no userspace fs_struct. This is
                    // absence only with affirmative kernel status and no user use.
                    const auto status = c.io.text(base + "/status");
                    bool kernel = false, seen = false, malformed = false;
                    if (status.value) {
                        std::istringstream lines(*status.value); std::string line;
                        while (std::getline(lines,line)) if (line.starts_with("Kthread:")) {
                            if (seen) malformed = true;
                            seen = true; kernel = trim(line.substr(8)) == "1";
                        }
                    }
                    if (!(before.absent && after.absent && kernel && !malformed &&
                          mapped.empty() && fds.value->empty()))
                        acquisitionProblem("Uninspected process filesystem reference: " + path);
                } else fileOwner(*before.value, path, "proc:" + std::string(reference) + ":" + pid);
            }
            for (const auto& fd : *fds.value) {
                if (!numeric(fd)) { problem("Malformed descriptor entry"); continue; }
                if (++handles > 262144) { problem("Descriptor scan limit"); break; }
                const auto path = base + "/fd/" + fd;
                const auto before = c.io.locate(path), after = c.io.locate(path);
                if (!before.value || !after.value || before.value != after.value) {
                    // Preserve positively observed storage and semantic handles
                    // even when collection must retry this descriptor set.
                    const auto target = c.io.canonical(path);
                    for (const auto* observation : {&before,&after}) if (observation->value) {
                        const auto& info = *observation->value;
                        if (info.block || target.value || !blockOwners(info.device).empty())
                            fileOwner(info,target.value.value_or(""),path);
                    }
                    acquisitionProblem("Descriptor changed or unreadable at PID " + pid); continue;
                }
                if (!before.value->block) {
                    const auto target = c.io.canonical(path), final_target = c.io.canonical(path);
                    if (!target.value || target.value != final_target.value) {
                        // Stable regular-file dev_t still proves its backing even
                        // if the display link is unreadable. Opaque types stay global.
                        fileOwner(*before.value,target.value.value_or(""),path,true);
                        if (final_target.value) fileOwner(*after.value,*final_target.value,path,true);
                        continue;
                    }
                    if (target.value->starts_with("mnt:[")) namespace_handles.insert(*target.value);
                    else fileOwner(*before.value, *target.value, path);
                    continue;
                }
                std::vector<std::string> owners;
                for (const auto& b : c.inventory.nodes)
                    if (b.observation.device_number == before.value->device)
                        owners.push_back(blockOwnershipKey(b.observation.kernel_name));
                if (owners.size() != 1) problem("Block handle cannot be attributed at PID " + pid);
                else c.evidence.claims.push_back({owners.front(), OwnershipUse::OpenBlockHandle, "proc:fd", "Open block handle at PID " + pid});
            }
            if (handles > 262144) break;
            const auto after_fds = c.io.list(base + "/fd");
            if (!after_fds.value ||
                std::set<std::string>(fds.value->begin(), fds.value->end()) !=
                std::set<std::string>(after_fds.value->begin(), after_fds.value->end()))
                acquisitionProblem("Descriptor set changed at PID " + pid);
            const auto after_mapped = mappings(base);
            for (const auto& records : {&mapped,&after_mapped}) for (const auto& record : *records) {
                if (mapped.contains(record) && after_mapped.contains(record)) continue;
                const auto& [major,minor,inode,path] = record; (void)inode;
                fileOwner(OwnershipPath{false,{major,minor}, true, false, std::nullopt, false},path,"Changed proc:maps:" + pid,true);
            }
            if (c.io.canonical(base + "/ns/mnt").value != ns.value)
                acquisitionProblem("Mount namespace changed at PID " + pid);
        }
        for (const auto& [path, before_tasks] : task_sets) {
            const auto after_tasks = c.io.list(path);
            if (!after_tasks.value || std::set<std::string>(after_tasks.value->begin(), after_tasks.value->end()) != before_tasks)
                acquisitionProblem("Thread set changed during scan");
        }
        const auto after = c.io.list("/proc");
        auto numericSet = [](const auto& values) {
            std::set<std::string> result;
            for (const auto& s : values) if (numeric(s)) result.insert(s);
            return result;
        };
        if (!after.value || numericSet(*after.value) != numericSet(*pids.value))
            acquisitionProblem("Process set changed during scan");
    }
    for (const auto& ns : namespace_handles)
        if (!namespaces.contains(ns)) problem("Mount namespace retained by a handle has no inspected task: " + ns);
    for (const auto& [ns, observation] : namespaces) {
        const auto again = c.io.text(observation.first);
        const auto link = observation.first.substr(0, observation.first.size() - std::string("mountinfo").size()) + "ns/mnt";
        if (!again.value || *again.value != observation.second || c.io.canonical(link).value != ns)
            acquisitionProblem("Mount namespace changed or became unreadable: " + ns);
    }
    const auto swaps = c.io.text("/proc/swaps");
    if (!swaps.value) acquisitionProblem("Cannot recheck active swap set");
    else {
        ResolvedInventorySnapshot parsed;
        parseSwaps(*swaps.value, parsed);
        auto keys = [](const auto& entries) {
            std::set<std::pair<std::string, std::string>> result;
            for (const auto& s : entries) result.emplace(s.path, s.type);
            return result;
        };
        if (!parsed.issues.empty()) problem("Malformed swap set");
        else if (keys(parsed.swaps) != keys(c.inventory.swaps)) acquisitionProblem("Swap set changed");
        for (const auto& swap : parsed.swaps) {
            if (const auto owner = c.pathOwner(swap.path))
                c.evidence.claims.push_back({*owner, OwnershipUse::Swap, "proc:swaps", swap.path});
            else problem("Active swap backing cannot be revalidated");
        }
        const auto again = c.io.text("/proc/swaps");
        if (!again.value) acquisitionProblem("Cannot finish swap recheck");
        else {
            ResolvedInventorySnapshot final;
            parseSwaps(*again.value, final);
            if (!final.issues.empty()) problem("Malformed swap set during recheck");
            else if (keys(final.swaps) != keys(parsed.swaps)) acquisitionProblem("Swap set changed during recheck");
        }
    }
    for (const auto& [path, before] : pmxcfs_paths)
        if (c.io.locate(path).value != std::optional<OwnershipPath>{before})
            problem("pmxcfs backing identity changed or became unreadable: " + path);
    if (pmxcfs_local && c.io.canonical("/etc/pve/local").value != pmxcfs_local)
        problem("pmxcfs local node identity changed");
    if (c.io.locate("/proc").value != proc_before.value) acquisitionProblem("Scanned procfs instance changed");
}
} // namespace
void readProcessOwnership(OwnershipScanContext& c) {
    ScanStage timing("ownership.process_collection");
    auto& gaps = c.evidence.coverage_gaps["processes"];
    // A settled final attempt is a bounded snapshot. All claims, observed
    // semantic gaps and earlier bracketing-pass failures remain retained.
    // In particular, a departed task does not release an observed opaque ring.
    constexpr unsigned max_attempts = 3;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        std::vector<OwnershipReason> acquisition_gaps;
        readProcessOwnershipPass(c, acquisition_gaps);
        if (acquisition_gaps.empty()) break;
        if (attempt + 1 == max_attempts)
            gaps.insert(gaps.end(), acquisition_gaps.begin(), acquisition_gaps.end());
    }
    c.evidence.coverage["processes"] = gaps.empty();
}
} // namespace drivelab
