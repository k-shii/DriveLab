#pragma once

#include "core/ownership_graph.h"
#include "platform/linux/linux_host_usage.h"

namespace drivelab {

// A read can distinguish absent metadata from permission/error/partial reads.
template<class T> struct OwnershipRead {
    std::optional<T> value;
    bool absent = false;
};
struct OwnershipPath {
    bool block = false;
    BlockDeviceNumber device; // rdev for a block node; filesystem dev otherwise
    bool filesystem_object = true; // regular file/directory, not an opaque device
    bool known_nonstorage = false; // native pipe or reviewed non-storage device
    std::optional<BlockDeviceNumber> character_device; // native character rdev, never inferred from its path
    bool socket = false; // sockets need separate queue/domain evidence; SCM_RIGHTS may retain storage files
    friend bool operator==(const OwnershipPath&, const OwnershipPath&) = default;
};
class OwnershipIo {
public:
    virtual ~OwnershipIo() = default;
    virtual OwnershipRead<std::string> text(const std::string& path) = 0;
    virtual OwnershipRead<std::vector<std::string>> list(const std::string& path) = 0;
    virtual OwnershipRead<std::string> canonical(const std::string& path) = 0;
    virtual OwnershipRead<OwnershipPath> locate(const std::string& path) = 0;
};
class NativeOwnershipIo final : public OwnershipIo {
public:
    OwnershipRead<std::string> text(const std::string& path) override;
    OwnershipRead<std::vector<std::string>> list(const std::string& path) override;
    OwnershipRead<std::string> canonical(const std::string& path) override;
    OwnershipRead<OwnershipPath> locate(const std::string& path) override;
};

struct ZfsVdevObservation {
    std::string key; // pool-scoped GUID or structural key
    std::string type;
    std::optional<std::string> path;
    std::vector<ZfsVdevObservation> children;
    bool complete = true;
    friend bool operator==(const ZfsVdevObservation&, const ZfsVdevObservation&) = default;
};
struct ZfsPoolObservation {
    std::string name;
    std::string guid;
    ZfsVdevObservation root;
    bool complete = true;
    friend bool operator==(const ZfsPoolObservation&, const ZfsPoolObservation&) = default;
};
struct ZfsOwnershipRead {
    bool complete = false;
    std::vector<ZfsPoolObservation> pools;
    std::string detail;
};
class ZfsOwnershipSource {
public:
    virtual ~ZfsOwnershipSource() = default;
    virtual ZfsOwnershipRead read() = 0;
};
// Public libzfs pool/config APIs, with the existing module pinned before init.
// Missing build/runtime support stays unavailable, never fabricated absence.
class NativeZfsOwnershipSource final : public ZfsOwnershipSource {
public:
    ZfsOwnershipRead read() override;
};

class FreshSignatureSource {
public:
    virtual ~FreshSignatureSource() = default;
    virtual FreshSignatures read(const ResolvedInventorySnapshot& inventory) = 0;
};
class NativeFreshSignatureSource final : public FreshSignatureSource {
public:
    explicit NativeFreshSignatureSource(BlockInventoryProvider& raw, OwnershipIo& io,
        PhysicalAssessmentScope scope = PhysicalAssessmentScope::AllPhysical)
        : raw_(raw), io_(io), scope_(scope) {}
    FreshSignatures read(const ResolvedInventorySnapshot& inventory) override;
private:
    BlockInventoryProvider& raw_;
    OwnershipIo& io_;
    PhysicalAssessmentScope scope_;
};

// C alone may perform fresh read-only signature probes. No commands, writes,
// execution capability, application integration, or B virtual identity changes.
class LinuxOwnershipSource {
public:
    LinuxOwnershipSource(OwnershipIo& io, ZfsOwnershipSource& zfs) : io_(io), zfs_(zfs) {}
    LinuxOwnershipSource(OwnershipIo& io, ZfsOwnershipSource& zfs, FreshSignatureSource& fresh)
        : io_(io), zfs_(zfs), fresh_(&fresh) {}
    OwnershipEvidence read(const ResolvedInventorySnapshot& inventory);
private:
    OwnershipIo& io_;
    ZfsOwnershipSource& zfs_;
    FreshSignatureSource* fresh_ = nullptr;
};

// Implementation helpers share a single scan's evidence and verified aliases.
struct OwnershipScanContext {
    OwnershipIo& io;
    const ResolvedInventorySnapshot& inventory;
    OwnershipEvidence& evidence;
    std::map<std::string, std::string> vg_names; // ambiguous names map to empty
    std::map<std::string, std::string> pools;
    std::optional<std::string> zfs_control_number;
    bool zfs_control_observed = false;
    bool accountZfsControl(const OwnershipPath& path);
    bool accountSocket(const OwnershipPath& path, const std::string& source);
    std::optional<std::string> pathOwner(const std::string& path);
    void issue(OwnershipReasonCode code, const std::string& node,
               const std::string& source, const std::string& detail);
};
void readProxmoxOwnership(OwnershipScanContext& context);
void readProcessOwnership(OwnershipScanContext& context);

} // namespace drivelab
