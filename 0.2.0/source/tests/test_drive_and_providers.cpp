#include "test_support.h"

#include "core/capability.h"
#include "providers/mock_providers.h"

#include <algorithm>

using namespace drivelab;

int main() {
    return test::run([] {
        std::vector<Drive> fixtures = MockInventoryProvider::fixtureDrives();
        DL_CHECK(fixtures.size() == 7);

        Drive original = fixtures.front();
        Drive moved = original;
        moved.current_path = "/dev/sdz";
        moved.identity.topology = "pci-0000:09:00.0-ata-9";
        DL_CHECK(original.identity.samePhysicalDevice(moved.identity));
        DL_CHECK(original.identity.lockKey().value() == moved.identity.lockKey().value());

        Drive mismatched = original;
        mismatched.identity.model = "Unexpected replacement model";
        DL_CHECK(original.identity.lockKey().value() == mismatched.identity.lockKey().value());
        DL_CHECK(!original.identity.samePhysicalDevice(mismatched.identity));

        DriveIdentity invalid;
        invalid.model = "Path-only device";
        invalid.capacity_bytes = 1000;
        DL_CHECK(!invalid.lockKey());
        DL_CHECK(invalid.lockKey().error().code == ErrorCode::InvalidIdentity);

        MockInventoryProvider inventory(fixtures);
        Result<std::vector<Drive>> scanned = inventory.scan();
        DL_CHECK(scanned);
        DL_CHECK(std::count_if(scanned.value().begin(), scanned.value().end(),
                              [](const Drive& drive) { return drive.isProtected(); }) == 3);
        Result<Drive> resolved = inventory.resolve(original.identity);
        DL_CHECK(resolved);
        DL_CHECK(resolved.value().current_path == original.current_path);

        CapabilityCatalog catalog;
        catalog.set(inventory.capability());
        catalog.set({CapabilityId::Latency, CapabilityAvailability::NotImplemented,
                     "none", "deferred"});
        DL_CHECK(catalog.all().size() == 2);
        DL_CHECK(catalog.find(CapabilityId::Inventory)->availability ==
                 CapabilityAvailability::Available);

        MockBenchmarkProvider benchmark;
        Result<OperationRequest> benchmark_plan = benchmark.plan(
            original, BenchmarkProfile::QuickRead);
        DL_CHECK(benchmark_plan);
        DL_CHECK(benchmark_plan.value().safety_class == SafetyClass::ReadOnly);
        DL_CHECK(benchmark_plan.value().controls.can_pause);
        DL_CHECK(benchmark_plan.value().controls.can_cancel);

        MockSanitizeProvider sanitize;
        Result<OperationRequest> sanitize_plan = sanitize.plan(
            original, SanitizeMethod::SecureErase);
        DL_CHECK(sanitize_plan);
        DL_CHECK(sanitize_plan.value().safety_class == SafetyClass::Irreversible);
        DL_CHECK(!sanitize_plan.value().controls.can_cancel);
    });
}
