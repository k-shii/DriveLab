#pragma once

#include "platform/linux/linux_inventory_source.h"
#include "providers/provider_interfaces.h"

namespace drivelab {

class LinuxInventoryProvider final : public BlockInventoryProvider {
public:
    explicit LinuxInventoryProvider(LinuxInventorySource& source);

    Result<BlockInventorySnapshot> scan() override;

private:
    LinuxInventorySource& source_;
};

}  // namespace drivelab
