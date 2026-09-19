#pragma once

#include "core/demo_model.h"
#include "core/result.h"

#include <cstddef>
#include <vector>

namespace drivelab {

class CoreApplication;

class DemoReadModel {
public:
    DemoReadModel(const DemoReadModel&) = default;
    DemoReadModel(DemoReadModel&&) noexcept = default;
    DemoReadModel& operator=(const DemoReadModel&) = default;
    DemoReadModel& operator=(DemoReadModel&&) noexcept = default;

    static Result<DemoReadModel> load(const CoreApplication& application);

    Result<void> refresh();
    [[nodiscard]] const DemoSnapshot& snapshot() const;
    [[nodiscard]] const std::vector<DemoDriveDetails>& drives() const;
    [[nodiscard]] const DemoDriveDetails* drive(std::size_t index) const;
    [[nodiscard]] Result<DriveId> driveId(std::size_t index) const;
    [[nodiscard]] Result<std::vector<DemoFeature>> features(std::size_t index) const;
    [[nodiscard]] Result<std::vector<DemoWorkflowDefinition>> workflows(
        std::size_t index,
        DemoFeature feature) const;

private:
    DemoReadModel(
        const CoreApplication& application,
        DemoSnapshot snapshot);

    const CoreApplication* application_;
    DemoSnapshot snapshot_;
};

}  // namespace drivelab
