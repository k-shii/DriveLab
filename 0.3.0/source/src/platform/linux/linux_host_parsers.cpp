#include "platform/linux/linux_host_usage.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace drivelab {
namespace {

template <typename T>
bool number(const std::string& text, T& output) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}

std::vector<std::string> words(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> result;
    std::string value;
    while (stream >> value) result.push_back(value);
    return result;
}

bool deviceNumber(const std::string& text, BlockDeviceNumber& value) {
    const auto colon = text.find(':');
    return colon != std::string::npos && number(text.substr(0, colon), value.major_number) &&
           number(text.substr(colon + 1), value.minor_number);
}

std::optional<std::string> unescape(const std::string& text) {
    std::string value;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c == 127) return std::nullopt;
        if (c != '\\') { value += static_cast<char>(c); continue; }
        if (i + 3 >= text.size()) return std::nullopt;
        const auto escape = text.substr(i, 4);
        if (escape == "\\040") value += ' ';
        else if (escape == "\\011") value += '\t';
        else if (escape == "\\012") value += '\n';
        else if (escape == "\\134") value += '\\';
        else return std::nullopt;
        i += 3;
    }
    return value;
}

void malformed(ResolvedInventorySnapshot& output, const char* source,
               const std::string& line) {
    output.issues.push_back({DiscoveryIssueCode::MalformedEvidence, "", source,
                            "Malformed record: " + line});
}

bool plainText(const std::string& value) {
    return !value.empty() && std::none_of(value.begin(), value.end(),
        [](unsigned char c) { return c < 32 || c == 127; });
}

bool token(const std::string& value) {
    return plainText(value) && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

}  // namespace

void parseMountInfo(const std::string& text, ResolvedInventorySnapshot& output) {
    std::istringstream input(text);
    std::string line;
    std::set<std::uint64_t> seen;
    if (text.empty()) malformed(output, "mountinfo", "Empty mount namespace record set");
    while (std::getline(input, line)) {
        if (input.eof()) break; // Do not attribute an unterminated final record.
        auto fields = words(line);
        const auto separator = std::find(fields.begin(), fields.end(), "-");
        const auto index = static_cast<std::size_t>(separator - fields.begin());
        MountEvidence mount;
        if (fields.size() < 10 || index < 6 || index + 4 != fields.size() ||
            !number(fields[0], mount.mount_id) || mount.mount_id == 0 ||
            !number(fields[1], mount.parent_id) ||
            !deviceNumber(fields[2], mount.device_number)) {
            malformed(output, "mountinfo", line);
            continue;
        }
        const auto root = unescape(fields[3]), point = unescape(fields[4]);
        const auto source = unescape(fields[index + 2]);
        if (!root || !point || !source || !root->starts_with("/") ||
            !point->starts_with("/") || !token(fields[index + 1])) {
            malformed(output, "mountinfo", line);
            continue;
        }
        mount.root = *root;
        mount.mount_point = *point;
        mount.source = *source;
        mount.options = fields[5];
        mount.optional_fields.assign(fields.begin() + 6, separator);
        mount.filesystem_type = fields[index + 1];
        mount.super_options = fields[index + 3];
        mount.subtree = mount.root != "/";
        if (!seen.insert(mount.mount_id).second) {
            output.issues.push_back({DiscoveryIssueCode::Conflict, "", "mountinfo",
                "Duplicate mount ID " + std::to_string(mount.mount_id) + "; records retained without attribution"});
        }
        output.mounts.push_back(std::move(mount));
    }
    if (!text.empty() && text.back() != '\n') {
        output.issues.push_back({DiscoveryIssueCode::MalformedEvidence, "", "mountinfo",
                                "Input lacks a terminating newline; possible truncated read"});
    }
    std::sort(output.mounts.begin(), output.mounts.end(), [](const auto& a, const auto& b) {
        return std::tie(a.mount_id, a.parent_id, a.device_number.major_number,
                        a.device_number.minor_number, a.root, a.mount_point,
                        a.options, a.optional_fields, a.filesystem_type, a.source, a.super_options) <
               std::tie(b.mount_id, b.parent_id, b.device_number.major_number,
                        b.device_number.minor_number, b.root, b.mount_point,
                        b.options, b.optional_fields, b.filesystem_type, b.source, b.super_options);
    });
}

void parseSwaps(const std::string& text, ResolvedInventorySnapshot& output) {
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) ||
        words(line) != std::vector<std::string>{"Filename", "Type", "Size", "Used", "Priority"}) {
        malformed(output, "swaps", "Missing or invalid /proc/swaps header");
        return;
    }
    std::set<std::string> paths;
    while (std::getline(input, line)) {
        if (input.eof()) break; // Do not attribute an unterminated final record.
        auto fields = words(line);
        SwapEvidence swap;
        const auto path = fields.empty() ? std::nullopt : unescape(fields[0]);
        if (fields.size() != 5 || !path || !path->starts_with("/") ||
            !number(fields[2], swap.size_kib) || !number(fields[3], swap.used_kib) ||
            !number(fields[4], swap.priority) || swap.used_kib > swap.size_kib) {
            malformed(output, "swaps", line);
            continue;
        }
        swap.path = *path;
        swap.type = fields[1];
        if (swap.type != "partition" && swap.type != "file") {
            output.issues.push_back({DiscoveryIssueCode::UnsupportedTopology, "", "swaps",
                                    "Unsupported swap type: " + line});
        }
        if (!paths.insert(swap.path).second) {
            output.issues.push_back({DiscoveryIssueCode::Conflict, "", "swaps",
                                    "Duplicate swap path retained without attribution: " + swap.path});
        }
        output.swaps.push_back(std::move(swap));
    }
    if (!text.empty() && text.back() != '\n') {
        output.issues.push_back({DiscoveryIssueCode::MalformedEvidence, "", "swaps",
                                "Input lacks a terminating newline; possible truncated read"});
    }
    std::sort(output.swaps.begin(), output.swaps.end(), [](const auto& a, const auto& b) {
        return std::tie(a.path, a.type, a.size_kib, a.used_kib, a.priority) <
               std::tie(b.path, b.type, b.size_kib, b.used_kib, b.priority);
    });
}

SignatureEvidence parseSignature(const HostRead<SignatureSourceRecord>& source,
                                 const std::string& node,
                                 std::vector<DiscoveryIssue>& issues) {
    SignatureEvidence evidence;
    issues.insert(issues.end(), source.issues.begin(), source.issues.end());
    if (!source.value) {
        issues.push_back({DiscoveryIssueCode::MissingEvidence, node, "signature",
                          "No signature observation; absence is not proof of an unsigned device"});
        return evidence;
    }
    std::map<std::string, std::set<std::string>> properties;
    for (const auto& [key, value] : source.value->properties) properties[key].insert(value);
    bool malformed_value = false, conflict = false, unsupported = false;
    for (const auto& [key, values] : properties) {
        if (values.size() != 1) conflict = true;
        for (const auto& value : values) {
            if (!plainText(value)) malformed_value = true;
        }
        if (key == "ID_FS_AMBIVALENT") conflict = true;
        else if (key == "ID_FS_TYPE" || key == "ID_FS_USAGE" || key == "ID_FS_UUID") {
            for (const auto& value : values) if (!token(value)) malformed_value = true;
        } else if (key != "ID_FS_LABEL") unsupported = true;
    }
    auto get = [&](const char* key) -> std::optional<std::string> {
        const auto it = properties.find(key);
        if (it == properties.end() || it->second.size() != 1) return std::nullopt;
        return *it->second.begin();
    };
    evidence.type = get("ID_FS_TYPE");
    evidence.usage = get("ID_FS_USAGE");
    evidence.uuid = get("ID_FS_UUID");
    evidence.label = get("ID_FS_LABEL");
    if (evidence.usage && *evidence.usage != "filesystem" && *evidence.usage != "raid" &&
        *evidence.usage != "crypto" && *evidence.usage != "other") unsupported = true;
    if (evidence.type == "swap" && evidence.usage && evidence.usage != "other") conflict = true;
    if (conflict) {
        evidence.state = SignatureState::Conflicting;
        issues.push_back({DiscoveryIssueCode::Conflict, node, "signature",
                          "Conflicting or ambivalent udev signature properties; raw properties retained in issue"});
    } else if (malformed_value) {
        evidence.state = SignatureState::Malformed;
        issues.push_back({DiscoveryIssueCode::MalformedEvidence, node, "signature",
                          "Malformed udev signature properties"});
    } else if (unsupported) {
        evidence.state = SignatureState::Unsupported;
        issues.push_back({DiscoveryIssueCode::UnsupportedTopology, node, "signature",
                          "Unrecognized signature property or usage; no ownership interpretation"});
    } else if (evidence.type && evidence.usage && source.issues.empty()) {
        evidence.state = SignatureState::Reported;
    } else {
        issues.push_back({DiscoveryIssueCode::MissingEvidence, node, "signature",
                          "Missing/incomplete/unreadable cached signature; no fresh device probe"});
    }
    if (conflict || malformed_value || unsupported || !source.issues.empty()) {
        for (const auto& [key, values] : properties) {
            for (const auto& value : values) {
                issues.push_back({DiscoveryIssueCode::Conflict, node, "signature." + key,
                                  "Untrusted observed property: " + value});
            }
        }
        // Never expose rejected fields as a selected filesystem fact.
        evidence.type.reset(); evidence.usage.reset();
        evidence.uuid.reset(); evidence.label.reset();
    }
    return evidence;
}

}  // namespace drivelab
