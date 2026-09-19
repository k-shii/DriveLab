#pragma once

#include <optional>
#include <string>
#include <vector>

namespace drivelab {

enum class CapabilityId {
    Inventory,
    Health,
    AtaInspection,
    Benchmark,
    Latency,
    Sanitize,
    ProcessExecution,
    Reporting
};

enum class CapabilityAvailability {
    Available,
    DisabledByMode,
    NotInstalled,
    NotImplemented,
    Unavailable
};

struct CapabilityStatus {
    CapabilityId id = CapabilityId::Inventory;
    CapabilityAvailability availability = CapabilityAvailability::Unavailable;
    std::string provider;
    std::string detail;
};

class CapabilityCatalog {
public:
    void set(CapabilityStatus status);
    [[nodiscard]] std::optional<CapabilityStatus> find(CapabilityId id) const;
    [[nodiscard]] const std::vector<CapabilityStatus>& all() const;

private:
    std::vector<CapabilityStatus> statuses_;
};

std::string capabilityName(CapabilityId id);
std::string capabilityAvailabilityName(CapabilityAvailability availability);

}  // namespace drivelab
