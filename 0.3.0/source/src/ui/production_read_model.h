#pragma once

#include "app/production_snapshot.h"
#include <memory>

namespace drivelab {

class CoreApplication;

std::string productionStatusLabel(DriveStatus status);
std::string productionStatusLabel(const ProductionDrive& drive);
std::string presentationText(const std::string& text);
std::string productionCapacity(const std::optional<std::uint64_t>& bytes);
std::string productionMedia(const BlockDeviceObservation& observation);
std::vector<std::string> productionReasonSummary(const ProductionDrive& drive);

// Presentation and selection only; no acquisition or storage authority.
class ProductionReadModel {
public:
    explicit ProductionReadModel(CoreApplication& application) : application_(application) {}
    Result<void> rescan();
    Result<void> startScan();
    Result<void> refresh();
    bool select(std::size_t index);
    [[nodiscard]] const ProductionSnapshot& snapshot() const { return *snapshot_; }
    [[nodiscard]] const ProductionDrive* selected() const;
    [[nodiscard]] std::optional<std::size_t> selectedIndex() const { return selected_; }
    [[nodiscard]] const std::string& selectionNotice() const { return selection_notice_; }
    [[nodiscard]] std::vector<std::string> overview(bool detailed = false) const;

private:
    CoreApplication& application_;
    std::shared_ptr<const ProductionSnapshot> snapshot_ = std::make_shared<const ProductionSnapshot>();
    std::optional<ProductionDrive> selection_anchor_;
    std::optional<std::size_t> selected_;
    std::string selection_notice_;
};

}  // namespace drivelab
