#pragma once

#include "core/resolved_inventory.h"
#include "providers/provider_interfaces.h"

#include <utility>

namespace drivelab {

template <typename T>
struct HostRead {
    std::optional<T> value;
    std::vector<DiscoveryIssue> issues;
};

enum class HostTextFile { MountInfo, Swaps };
struct HostPathStatus {
    bool is_block = false;
    std::optional<BlockDeviceNumber> device_number;
    friend bool operator==(const HostPathStatus&, const HostPathStatus&) = default;
};

class LinuxHostFiles {
public:
    virtual ~LinuxHostFiles() = default;
    virtual HostRead<std::string> read(HostTextFile file) = 0;
    // Metadata stat only: never open/read block-device data.
    virtual HostRead<HostPathStatus> inspect(const std::string& path) = 0;
};

class ProcHostFiles final : public LinuxHostFiles {
public:
    HostRead<std::string> read(HostTextFile file) override;
    HostRead<HostPathStatus> inspect(const std::string& path) override;
};

struct SignatureSourceRecord {
    // Preserve duplicate properties for deterministic conflict handling.
    std::vector<std::pair<std::string, std::string>> properties;
};

class LinuxSignatureSource {
public:
    virtual ~LinuxSignatureSource() = default;
    virtual HostRead<SignatureSourceRecord> read(const BlockDeviceObservation& node) = 0;
};

class UdevSignatureSource final : public LinuxSignatureSource {
public:
    HostRead<SignatureSourceRecord> read(const BlockDeviceObservation& node) override;
};

// Structured /proc parsers; failed records remain issues, other records survive.
void parseMountInfo(const std::string& text, ResolvedInventorySnapshot& output);
void parseSwaps(const std::string& text, ResolvedInventorySnapshot& output);
SignatureEvidence parseSignature(const HostRead<SignatureSourceRecord>& source,
                                 const std::string& node,
                                 std::vector<DiscoveryIssue>& issues);

// Composes the accepted raw provider; never returns Drive or production status.
class LinuxResolvedInventoryProvider {
public:
    LinuxResolvedInventoryProvider(BlockInventoryProvider& inventory,
                                   LinuxHostFiles& files, LinuxSignatureSource& signatures);
    Result<ResolvedInventorySnapshot> scan();
    // A fresh scan every time, with no cached path -> identity association.
    Result<std::string> resolvePath(const DriveId& expected);

private:
    BlockInventoryProvider& inventory_;
    LinuxHostFiles& files_;
    LinuxSignatureSource& signatures_;
};

}  // namespace drivelab
