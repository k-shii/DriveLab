#include "app/native_application.h"

#ifdef DRIVELAB_NATIVE_LINUX
#include "platform/linux/linux_production_source.h"

namespace drivelab {
namespace {

class NativeProductionSource final : public ProductionInventorySource {
public:
    Result<ResolvedInventorySnapshot> scanInventory() override { return pipeline_.scanInventory(); }
    OwnershipEvidence readOwnership(const ResolvedInventorySnapshot& inventory) override {
        return pipeline_.readOwnership(inventory);
    }
private:
    UdevSysfsInventorySource source_;
    LinuxInventoryProvider raw_{source_};
    ProcHostFiles files_;
    UdevSignatureSource signatures_;
    NativeOwnershipIo io_;
    NativeZfsOwnershipSource zfs_;
    NativeFreshSignatureSource fresh_{raw_, io_, PhysicalAssessmentScope::Disks};
    LinuxProductionSource pipeline_{raw_, files_, signatures_, io_, zfs_, fresh_};
};

}  // namespace
}  // namespace drivelab
#endif

namespace drivelab {

Result<std::unique_ptr<CoreApplication>> createNativeApplication(ILogger& logger) {
#ifdef DRIVELAB_NATIVE_LINUX
    AppConfig config;
    config.mode = ExecutionMode::Real;
    return CoreApplication::createProduction(config, logger, std::make_unique<NativeProductionSource>());
#else
    (void)logger;
    return Result<std::unique_ptr<CoreApplication>>::failure({
        ErrorCode::Unavailable, "NativeApplication",
        "Live discovery requires a Linux build with DRIVELAB_BUILD_LINUX_INVENTORY=ON"});
#endif
}

}  // namespace drivelab
