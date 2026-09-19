#include "platform/linux/linux_production_source.h"

namespace drivelab {
Result<ResolvedInventorySnapshot> LinuxProductionSource::scanInventory() {
    return resolved_.scan();
}
OwnershipEvidence LinuxProductionSource::readOwnership(const ResolvedInventorySnapshot& inventory) {
    return ownership_.read(inventory);
}
} // namespace drivelab
