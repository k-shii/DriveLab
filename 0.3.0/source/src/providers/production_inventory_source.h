#pragma once

#include "core/ownership_graph.h"
#include "core/result.h"

namespace drivelab {

// Acquire inventory first, then one host evidence set against that immutable
// inventory. Core, not a provider or frontend, performs the assessment.

class ProductionInventorySource {
public:
    virtual ~ProductionInventorySource() = default;
    virtual Result<ResolvedInventorySnapshot> scanInventory() = 0;
    virtual OwnershipEvidence readOwnership(const ResolvedInventorySnapshot& inventory) = 0;
};

}  // namespace drivelab
