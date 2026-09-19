#include "core/resolved_inventory.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace drivelab {
namespace {

void issue(ResolvedBlockNode& node, DiscoveryIssueCode code,
           std::string field, std::string message) {
    node.issues.push_back({code, node.observation.kernel_name,
                          std::move(field), std::move(message)});
}

std::optional<std::string> hexIdentifier(std::string value, std::size_t digits,
                                        bool formatted = false) {
    if (value.starts_with("0x") || value.starts_with("0X")) value.erase(0, 2);
    std::string compact;
    if (formatted && digits == 16 && value.size() == 23) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i % 3 == 2) { if (value[i] != ' ') return std::nullopt; }
            else compact += value[i];
        }
    } else if (formatted && digits == 32 && value.size() == 36) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (value[i] != '-') return std::nullopt;
            } else compact += value[i];
        }
    } else compact = value;
    if (compact.size() != digits) return std::nullopt;
    for (char& c : compact) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return std::nullopt;
    }
    if (compact.find_first_not_of('0') == std::string::npos ||
        compact.find_first_not_of('f') == std::string::npos) return std::nullopt;
    return compact;
}

bool trustworthyText(const std::optional<std::string>& text) {
    if (!text || text->empty() || text->front() == ' ' || text->back() == ' ') return false;
    std::string lower;
    for (unsigned char c : *text) {
        if (c < 32 || c >= 127) return false;
        lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    const std::set<std::string> placeholders{
        "unknown", "none", "null", "n/a", "na", "not specified",
        "default", "default string", "to be filled by o.e.m.", "123456789",
        "0123456789", "serial", "serialnumber"
    };
    return !placeholders.contains(lower) &&
           lower.find_first_not_of('0') != std::string::npos &&
           lower.find_first_not_of('f') != std::string::npos;
}

std::string component(const std::string& value) {
    // Case-sensitive length-prefixed bytes avoid delimiter collisions.
    return std::to_string(value.size()) + ":" + value;
}

void identify(ResolvedBlockNode& node) {
    const auto& raw = node.observation;
    if (raw.node_type != BlockNodeType::Disk ||
        raw.observation_kind == BlockObservationKind::VirtualOrPseudo) {
        issue(node, DiscoveryIssueCode::UnsupportedTopology, "identity",
              "Only whole hardware-addressed nodes are identity candidates; no virtual ownership traversal");
        return;
    }
    bool blocked = false;
    for (const auto& problem : raw.issues) {
        if (problem.field == "wwn" || problem.field == "nvme_eui" ||
            problem.field == "nvme_nguid" || problem.field == "serial" ||
            problem.field == "model" || problem.field == "capacity_bytes" ||
            problem.field == "capacity_sectors_512" || problem.field == "node_type" ||
            problem.code == BlockInventoryIssueCode::DeviceDisappeared) {
            blocked = true;
            issue(node, DiscoveryIssueCode::Conflict, "identity",
                  "Raw identity evidence is incomplete, conflicting, or unreadable; see inventory issues");
        }
    }
    const bool nvme = raw.nvme_eui || raw.nvme_nguid || raw.transport == "nvme" ||
        (raw.wwn && (raw.wwn->starts_with("eui.") || raw.wwn->starts_with("nvme.") ||
                     raw.wwn->starts_with("uuid.")));
    node.identity.scope = nvme ? IdentityScope::NvmeNamespace : IdentityScope::WholeDevice;
    auto parse = [&](const std::optional<std::string>& text, std::size_t size,
                     const char* field, const char* prefix) -> std::optional<std::string> {
        if (!text) return std::nullopt;
        auto value = hexIdentifier(*text, size, true);
        if (!value) {
            blocked = true;
            issue(node, DiscoveryIssueCode::MalformedEvidence, field,
                  "Unsupported identifier spelling, length, or sentinel value");
            return std::nullopt;
        }
        const std::string alias = std::string(prefix) + *value;
        node.identity.strong_aliases.push_back(alias);
        return alias;
    };
    const auto nguid = parse(raw.nvme_nguid, 32, "nvme_nguid", "prod:nvme-nguid:");
    const auto eui = parse(raw.nvme_eui, 16, "nvme_eui", "prod:nvme-eui:");
    std::optional<std::string> wwn;
    if (raw.wwn) {
        if (nvme) {
            // Linux may expose NGUID or EUI with the same eui. prefix.
            // Corroboration by the separately typed namespace attribute is required.
            const auto& text = *raw.wwn;
            std::optional<std::string> alias;
            if (text.starts_with("eui.")) {
                const auto value = text.substr(4);
                if (auto hex = hexIdentifier(value, 16)) alias = "prod:nvme-eui:" + *hex;
                else if (auto long_hex = hexIdentifier(value, 32)) alias = "prod:nvme-nguid:" + *long_hex;
            }
            if (!alias || (alias != nguid && alias != eui)) {
                blocked = true;
                issue(node, DiscoveryIssueCode::Conflict, "wwn",
                      "Namespace WWID is unsupported or disagrees with typed EUI/NGUID evidence");
            }
        } else {
            std::string value = *raw.wwn;
            if (value.starts_with("naa.")) value.erase(0, 4);
            auto hex = hexIdentifier(value, 16);
            if (!hex && value.size() == 32 && value.front() == '6')
                hex = hexIdentifier(value, 32);
            if (!hex) {
                blocked = true;
                issue(node, DiscoveryIssueCode::MalformedEvidence, "wwn",
                      "WWN must be non-sentinel 64-bit hex or a 128-bit NAA-6 value");
            } else {
                wwn = "prod:wwn:" + *hex;
                node.identity.strong_aliases.push_back(*wwn);
            }
        }
    }
    std::optional<std::string> selected = nvme ? (nguid ? nguid : eui) : wwn;
    // USB bridge serials and unknown transport lack adequate fallback provenance.
    const bool fallback_transport = raw.transport == "ata" || raw.transport == "scsi" ||
                                    raw.transport == "sas";
    if (!nvme && !blocked && fallback_transport &&
        trustworthyText(raw.serial) && trustworthyText(raw.model) &&
        raw.capacity_bytes && *raw.capacity_bytes > 0) {
        node.identity.fallback_fingerprint = "prod:serial:" + component(*raw.serial) +
            component(*raw.model) + ":" + std::to_string(*raw.capacity_bytes);
        if (!selected) selected = node.identity.fallback_fingerprint;
    }
    if (!selected) {
        issue(node, DiscoveryIssueCode::MissingEvidence, "identity",
              "Insufficient trustworthy stable identity evidence; no path-derived fallback");
    } else if (!blocked) {
        node.identity.id = DriveId{*selected};
        node.identity.state = ResolutionState::Resolved;
    }
}

bool validPath(const std::optional<std::string>& path) {
    if (!path || !path->starts_with("/dev/") || path->size() <= 5 ||
        path->back() == '/' || path->find("//") != std::string::npos) return false;
    const auto terminated = *path + "/";
    return terminated.find("/../") == std::string::npos &&
           terminated.find("/./") == std::string::npos &&
           std::none_of(path->begin(), path->end(), [](unsigned char c) { return c < 32 || c == 127; });
}

}  // namespace

void sortDiscoveryIssues(std::vector<DiscoveryIssue>& issues) {
    std::sort(issues.begin(), issues.end(), [](const auto& a, const auto& b) {
        return std::tie(a.code, a.node, a.field, a.message, a.native_error, a.mount_id) <
               std::tie(b.code, b.node, b.field, b.message, b.native_error, b.mount_id);
    });
    issues.erase(std::unique(issues.begin(), issues.end()), issues.end());
}

ResolvedInventorySnapshot resolveBlockIdentities(const BlockInventorySnapshot& raw) {
    ResolvedInventorySnapshot result;
    result.inventory_issues = raw.issues;
    auto rawIssueLess = [](const auto& a, const auto& b) {
        return std::tie(a.code, a.kernel_name, a.field, a.message, a.native_error) <
               std::tie(b.code, b.kernel_name, b.field, b.message, b.native_error);
    };
    std::sort(result.inventory_issues.begin(), result.inventory_issues.end(), rawIssueLess);
    std::map<std::string, std::vector<std::size_t>> claims;
    for (const auto& observation : raw.devices) {
        ResolvedBlockNode node;
        node.observation = observation;
        identify(node);
        if (validPath(observation.current_path)) node.current_path = observation.current_path;
        else issue(node, DiscoveryIssueCode::MissingEvidence, "current_path",
                   "No usable current Linux device path");
        const auto index = result.nodes.size();
        if (node.identity.id) claims["id:" + node.identity.id->value].push_back(index);
        for (const auto& alias : node.identity.strong_aliases) claims["id:" + alias].push_back(index);
        if (node.identity.fallback_fingerprint)
            claims["fallback:" + *node.identity.fallback_fingerprint].push_back(index);
        claims["name:" + observation.kernel_name].push_back(index);
        if (node.current_path) claims["path:" + *node.current_path].push_back(index);
        if (observation.device_number) {
            claims["dev:" + std::to_string(observation.device_number->major_number) + ":" +
                   std::to_string(observation.device_number->minor_number)].push_back(index);
        }
        result.nodes.push_back(std::move(node));
    }
    for (auto& [claim, indices] : claims) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (indices.size() < 2) continue;
        for (auto index : indices) {
            auto& node = result.nodes[index];
            node.identity.id.reset();
            node.identity.state = ResolutionState::Ambiguous;
            node.current_path.reset();
            issue(node, DiscoveryIssueCode::AmbiguousIdentity, "identity",
                  "Multiple observations claim " + claim + "; no arbitrary selection or multipath merge");
        }
    }
    for (auto& node : result.nodes) {
        sortDiscoveryIssues(node.issues);
        std::sort(node.identity.strong_aliases.begin(), node.identity.strong_aliases.end());
        std::sort(node.observation.child_kernel_names.begin(), node.observation.child_kernel_names.end());
        std::sort(node.observation.issues.begin(), node.observation.issues.end(), rawIssueLess);
    }
    std::sort(result.nodes.begin(), result.nodes.end(), [](const auto& a, const auto& b) {
        return std::tie(a.observation.kernel_name, a.observation.current_path,
                        a.observation.sysfs_path, a.observation.wwn, a.observation.nvme_eui,
                        a.observation.nvme_nguid, a.observation.serial, a.observation.model,
                        a.observation.capacity_bytes, a.observation.transport) <
               std::tie(b.observation.kernel_name, b.observation.current_path,
                        b.observation.sysfs_path, b.observation.wwn, b.observation.nvme_eui,
                        b.observation.nvme_nguid, b.observation.serial, b.observation.model,
                        b.observation.capacity_bytes, b.observation.transport);
    });
    return result;
}

Result<std::string> resolveCurrentPath(const ResolvedInventorySnapshot& snapshot,
                                      const DriveId& expected) {
    const ResolvedBlockNode* found = nullptr;
    for (const auto& node : snapshot.nodes) {
        if (node.identity.id != expected) continue;
        if (found || node.identity.state != ResolutionState::Resolved) {
            return Result<std::string>::failure({ErrorCode::InvalidIdentity,
                "IdentityResolver", "Identity is ambiguous in the current snapshot"});
        }
        found = &node;
    }
    if (!found || !found->current_path) {
        return Result<std::string>::failure({ErrorCode::NotFound, "IdentityResolver",
            "No uniquely resolved current path for this identity in this snapshot"});
    }
    return Result<std::string>::success(*found->current_path);
}

}  // namespace drivelab
