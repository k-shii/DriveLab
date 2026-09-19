#pragma once

#include "providers/provider_interfaces.h"

#include <cstddef>
#include <vector>

namespace drivelab {

class MockInventoryProvider final : public InventoryProvider {
public:
    MockInventoryProvider();
    explicit MockInventoryProvider(std::vector<Drive> drives);

    [[nodiscard]] CapabilityStatus capability() const override;
    Result<std::vector<Drive>> scan() override;
    Result<Drive> resolve(const DriveIdentity& expected) override;

    void replace(std::size_t index, Drive drive);
    [[nodiscard]] const std::vector<Drive>& drives() const;

    static std::vector<Drive> fixtureDrives();

private:
    std::vector<Drive> drives_;
};

class MockHealthProvider final : public HealthProvider {
public:
    [[nodiscard]] CapabilityStatus capability() const override;
    Result<OperationRequest> plan(const Drive& drive, HealthTestType test) const override;
};

class MockAtaProvider final : public AtaProvider {
public:
    [[nodiscard]] CapabilityStatus capability() const override;
    Result<OperationRequest> plan(const Drive& drive, AtaInspectionType inspection) const override;
};

class MockBenchmarkProvider final : public BenchmarkProvider {
public:
    [[nodiscard]] CapabilityStatus capability() const override;
    Result<OperationRequest> plan(const Drive& drive, BenchmarkProfile profile) const override;
};

class MockSanitizeProvider final : public SanitizeProvider {
public:
    [[nodiscard]] CapabilityStatus capability() const override;
    Result<OperationRequest> plan(const Drive& drive, SanitizeMethod method) const override;
};

}  // namespace drivelab
