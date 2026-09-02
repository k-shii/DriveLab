#include "ui/demo_read_model.h"

#include "app/core_application.h"

#include <utility>

namespace drivelab {

Result<DemoReadModel> DemoReadModel::load(const CoreApplication& application) {
    Result<DemoSnapshot> snapshot = application.demoSnapshot();
    if (!snapshot) return Result<DemoReadModel>::failure(snapshot.error());
    return Result<DemoReadModel>::success(
        DemoReadModel(application, std::move(snapshot.value())));
}

DemoReadModel::DemoReadModel(
    const CoreApplication& application,
    DemoSnapshot snapshot)
    : application_(&application), snapshot_(std::move(snapshot)) {}

Result<void> DemoReadModel::refresh() {
    Result<DemoSnapshot> snapshot = application_->demoSnapshot();
    if (!snapshot) return Result<void>::failure(snapshot.error());
    snapshot_ = std::move(snapshot.value());
    return Result<void>::success();
}

const DemoSnapshot& DemoReadModel::snapshot() const {
    return snapshot_;
}

const std::vector<DemoDriveDetails>& DemoReadModel::drives() const {
    return snapshot_.drives;
}

const DemoDriveDetails* DemoReadModel::drive(std::size_t index) const {
    return index < snapshot_.drives.size() ? &snapshot_.drives[index] : nullptr;
}

Result<DriveId> DemoReadModel::driveId(std::size_t index) const {
    const DemoDriveDetails* selected = drive(index);
    if (!selected) {
        return Result<DriveId>::failure({
            ErrorCode::NotFound,
            "DemoReadModel",
            "Demo drive index is outside the Core snapshot"
        });
    }
    return selected->drive.identity.driveId();
}

Result<std::vector<DemoFeature>> DemoReadModel::features(std::size_t index) const {
    Result<DriveId> id = driveId(index);
    if (!id) return Result<std::vector<DemoFeature>>::failure(id.error());
    return application_->demoFeatures(id.value());
}

Result<std::vector<DemoWorkflowDefinition>> DemoReadModel::workflows(
    std::size_t index,
    DemoFeature feature) const {
    Result<DriveId> id = driveId(index);
    if (!id) {
        return Result<std::vector<DemoWorkflowDefinition>>::failure(id.error());
    }
    return application_->demoWorkflows(id.value(), feature);
}

}  // namespace drivelab
