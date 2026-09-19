#include "core/capability.h"

#include <algorithm>
#include <utility>

namespace drivelab {

void CapabilityCatalog::set(CapabilityStatus status) {
    auto existing = std::find_if(statuses_.begin(), statuses_.end(), [&](const auto& item) {
        return item.id == status.id;
    });
    if (existing == statuses_.end()) {
        statuses_.push_back(std::move(status));
    } else {
        *existing = std::move(status);
    }
}

std::optional<CapabilityStatus> CapabilityCatalog::find(CapabilityId id) const {
    auto existing = std::find_if(statuses_.begin(), statuses_.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (existing == statuses_.end()) return std::nullopt;
    return *existing;
}

const std::vector<CapabilityStatus>& CapabilityCatalog::all() const {
    return statuses_;
}

std::string capabilityName(CapabilityId id) {
    switch (id) {
        case CapabilityId::Inventory: return "Inventory";
        case CapabilityId::Health: return "S.M.A.R.T";
        case CapabilityId::AtaInspection: return "ATA / HPA";
        case CapabilityId::Benchmark: return "Benchmark";
        case CapabilityId::Latency: return "Latency";
        case CapabilityId::Sanitize: return "Sanitize";
        case CapabilityId::ProcessExecution: return "Process execution";
        case CapabilityId::Reporting: return "Reporting";
    }
    return "Unknown";
}

std::string capabilityAvailabilityName(CapabilityAvailability availability) {
    switch (availability) {
        case CapabilityAvailability::Available: return "AVAILABLE";
        case CapabilityAvailability::DisabledByMode: return "DISABLED BY MODE";
        case CapabilityAvailability::NotInstalled: return "NOT INSTALLED";
        case CapabilityAvailability::NotImplemented: return "NOT IMPLEMENTED";
        case CapabilityAvailability::Unavailable: return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}

}  // namespace drivelab
