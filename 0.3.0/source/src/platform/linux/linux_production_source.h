#pragma once

#include "platform/linux/linux_inventory_provider.h"
#include "platform/linux/linux_ownership_source.h"
#include "providers/production_inventory_source.h"

namespace drivelab {

// Native and injected sources use the identical composition.
// Referenced sources must outlive the pipeline.
class LinuxProductionSource final : public ProductionInventorySource {
public:
    LinuxProductionSource(BlockInventoryProvider& raw, LinuxHostFiles& files,
                          LinuxSignatureSource& signatures, OwnershipIo& io,
                          ZfsOwnershipSource& zfs, FreshSignatureSource& fresh)
        : resolved_(raw, files, signatures), ownership_(io, zfs, fresh) {}
    Result<ResolvedInventorySnapshot> scanInventory() override;
    OwnershipEvidence readOwnership(const ResolvedInventorySnapshot& inventory) override;
private:
    LinuxResolvedInventoryProvider resolved_;
    LinuxOwnershipSource ownership_;
};

}  // namespace drivelab
