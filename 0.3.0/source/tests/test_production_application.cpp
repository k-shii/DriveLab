#include "production_fixture.h"
#include "app/core_application.h"
#include "app/core_cli.h"
#include "ui/production_read_model.h"

#include <sstream>
#include "core/scan_control.h"
#include <stdexcept>

using namespace drivelab;
using production_test::Fixture;

template<class T> concept HasDiagnostics = requires(T value) { value.smart; } ||
    requires(T value) { value.health; } || requires(T value) { value.ata; } ||
    requires(T value) { value.benchmarks; };
static_assert(!HasDiagnostics<ProductionDrive>);
static_assert(!HasDiagnostics<ProductionSnapshot>);

namespace {
std::unique_ptr<CoreApplication> application(Fixture& f, ILogger& logger) {
    AppConfig config;
    config.mode = ExecutionMode::Real;
    auto app = CoreApplication::createProduction(config, logger, f.pipeline());
    DL_CHECK(app);
    return std::move(app.value());
}

void pipelineAndModes() {
    Fixture f;
    NullLogger logger;
    auto app = application(f, logger);
    DL_CHECK(f.source.scans == 0);
    ProductionReadModel view(*app);
    DL_CHECK(view.rescan());
    DL_CHECK(f.source.scans == 1 && f.files.reads >= 2 && f.signatures.reads == 1 && f.fresh.reads >= 1);
    DL_CHECK(view.snapshot().drives.size() == 1);
    const auto& drive = view.snapshot().drives.front();
    DL_CHECK(drive.identity.id && drive.identity.id->value.starts_with("prod:"));
    DL_CHECK(drive.observation.model == "Integration Disk");
    DL_CHECK(drive.status == DriveStatus::Ready);
    DL_CHECK(productionStatusLabel(drive.status) == "READY");
    DL_CHECK(!app->snapshot() && !app->demoSnapshot());
    DL_CHECK(!app->scanDemoInventory());
    DL_CHECK(view.select(0));
    const auto lines = view.overview(true);
    for (const auto& line : lines) {
        DL_CHECK(line.find("PASSED") == std::string::npos);
        DL_CHECK(line.find("Temperature") == std::string::npos);
        DL_CHECK(line.find("Power-on") == std::string::npos);
    }
    for (const auto& capability : app->capabilities().all()) DL_CHECK(capability.provider != "mock");
    const auto mock = MockInventoryProvider::fixtureDrives().front();
    DL_CHECK(!app->submitHealth(mock, HealthTestType::Short));
    DL_CHECK(!app->submitAtaInspection(mock, AtaInspectionType::Security));
    DL_CHECK(!app->submitBenchmark(mock, BenchmarkProfile::QuickRead));
    DL_CHECK(!app->submitSanitize(mock, SanitizeMethod::Overwrite));
    DL_CHECK(app->jobs().jobs().empty());
    DL_CHECK(app->processRunner().planned().empty());

    int demo = 0, live = 0;
    auto demo_runner = [&] { ++demo; return 7; };
    auto live_runner = [&] { ++live; return 9; };
    std::ostringstream out, err;
    for (const auto& flag : {"--help", "--version", "--dry-run"})
        DL_CHECK(runApplicationCli({flag}, "drivelab", out, err, demo_runner, live_runner) == 0);
    DL_CHECK(live == 0 && demo == 0);
    DL_CHECK(runApplicationCli({"--demo"}, "drivelab", out, err, demo_runner, live_runner) == 7);
    DL_CHECK(demo == 1 && live == 0);
    DL_CHECK(runApplicationCli({}, "drivelab", out, err, demo_runner, live_runner) == 9);
    DL_CHECK(live == 1 && demo == 1);
    DL_CHECK(runApplicationCli({"--invalid"}, "drivelab", out, err, demo_runner, live_runner) == 1);
    DL_CHECK(live == 1 && f.source.scans == 1);

    AppConfig config;
    config.mode = ExecutionMode::Demo;
    auto demo_app = CoreApplication::createDemo(config, logger);
    DL_CHECK(demo_app && demo_app.value()->demoSnapshot().value().drives.size() == 7);
    DL_CHECK(!demo_app.value()->rescanProduction() && !demo_app.value()->productionSnapshot());
    DL_CHECK(demo_app.value()->processRunner().planned().empty());
    DL_CHECK(!CoreApplication::createProduction(config, logger, f.pipeline()));
    config.mode = ExecutionMode::Real;
    DL_CHECK(!CoreApplication::createProduction(config, logger, nullptr));
}

void rescanAndClassification() {
    Fixture f;
    NullLogger logger;
    auto app = application(f, logger);
    ProductionReadModel view(*app);
    DL_CHECK(view.rescan() && view.select(0));
    const auto id = view.selected()->identity.id;
    f.host.io.lists["/proc/1/fd"].value = std::vector<std::string>{"3"};
    f.host.io.paths["/proc/1/fd/3"] = {OwnershipPath{true,{8,0},true,false,std::nullopt,false},false};
    DL_CHECK(view.rescan());
    DL_CHECK(view.selected() && view.selected()->status == DriveStatus::Busy);
    DL_CHECK(productionStatusLabel(view.selected()->status) == "BUSY");
    f.host.io.lists["/proc/1/fd"].value = std::vector<std::string>{};

    // Rename the path and kernel node, keeping all stable identity evidence.
    const auto record = f.source.data.records.front();
    f.host.add("sdc", {8,32});
    f.host.inventory.nodes.erase(f.host.inventory.nodes.begin());
    f.sync();
    f.source.data.records[0].wwn = record.wwn;
    f.source.data.records[0].serial = record.serial;
    f.host.io.lists["/sys/class/block"].value = std::vector<std::string>{"sdc"};
    DL_CHECK(view.rescan());
    DL_CHECK(view.selected() && view.selected()->identity.id == id);
    DL_CHECK(view.selected()->current_path == "/dev/sdc");
    DL_CHECK(view.selected()->status == DriveStatus::Ready);

    f.host.partition("sdc1", "sdc", {8,33});
    f.sync();
    f.source.data.records[0].wwn = record.wwn;
    f.source.data.records[0].serial = record.serial;
    f.host.io.texts["/proc/self/mountinfo"].value =
        "1 0 8:33 / / rw - ext4 /dev/sdc1 rw\n2 1 0:3 / /proc rw - proc proc rw\n";
    DL_CHECK(view.rescan());
    DL_CHECK(view.snapshot().inventory.nodes.size() == 2);
    DL_CHECK(view.selected() && view.selected()->status == DriveStatus::Protected);
    DL_CHECK(productionStatusLabel(view.selected()->status) == "RESTRICTED");
    DL_CHECK(view.snapshot().ownership.edges.size() >= 1);

    // Same path/WWN with a changed serial is not a retained selection.
    f.source.data.records[0].serial = "REPLACEMENT";
    DL_CHECK(view.rescan() && !view.selected());
    DL_CHECK(!view.selectionNotice().empty());
    DL_CHECK(view.select(0));
    f.source.data.records.clear();
    f.host.io.lists["/sys/class/block"].value = std::vector<std::string>{};
    DL_CHECK(view.rescan());
    DL_CHECK(view.snapshot().drives.empty() && !view.selected());
    DL_CHECK(!view.select(0));
}

void gapsAndFailures() {
    Fixture f;
    NullLogger logger;
    auto app = application(f, logger);
    ProductionReadModel view(*app);
    f.host.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
    f.host.zfs.result = {false, {}, "optional provider unavailable"};
    DL_CHECK(view.rescan());
    DL_CHECK(view.snapshot().drives.size() == 1);
    DL_CHECK(view.snapshot().drives[0].status == DriveStatus::Unknown);
    DL_CHECK(productionStatusLabel(view.snapshot().drives[0].status) == "CAUTION");
    f.host.io.lists.erase("/sys/module/zfs");
    f.host.zfs.result = {true, {}, ""};
    f.fresh.state = FreshSignatureState::PermissionDenied;
    DL_CHECK(view.rescan() && view.snapshot().drives[0].status == DriveStatus::Unknown);
    f.fresh.state = FreshSignatureState::None;
    f.source.data.records[0].wwn.reset();
    f.source.data.records[0].serial.reset();
    DL_CHECK(view.rescan() && view.snapshot().drives.size() == 1);
    DL_CHECK(!view.snapshot().drives[0].identity.id);
    DL_CHECK(view.snapshot().drives[0].status == DriveStatus::Unknown);
    DL_CHECK(view.select(0) && view.rescan() && !view.selected());

    f.sync();
    DL_CHECK(view.rescan() && view.snapshot().drives[0].status == DriveStatus::Ready);
    DL_CHECK(view.select(0));
    f.source.fail = true;
    DL_CHECK(!view.rescan());
    DL_CHECK(view.snapshot().scan_error && view.snapshot().drives.empty() && !view.selected());
    const auto generation = view.snapshot().generation;
    f.source.fail = false;
    DL_CHECK(view.rescan() && view.snapshot().generation > generation);
    DL_CHECK(view.snapshot().drives.size() == 1 && !view.selected());
}


class GatedSource final : public ProductionInventorySource {
public:
    explicit GatedSource(std::unique_ptr<ProductionInventorySource> delegate) : delegate_(std::move(delegate)) {}
    std::atomic_bool release{false}, entered{false};
    bool fail = false;
    int inventories = 0, proofs = 0;
    Result<ResolvedInventorySnapshot> scanInventory() override {
        ++inventories; return delegate_->scanInventory();
    }
    OwnershipEvidence readOwnership(const ResolvedInventorySnapshot& inventory) override {
        ++proofs;
        entered = true;
        while (!release) {
            checkScanCancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (fail) throw std::runtime_error("Injected ownership failure");
        return delegate_->readOwnership(inventory);
    }
private:
    std::unique_ptr<ProductionInventorySource> delegate_;
};
void awaitProof(GatedSource& source) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!source.entered) {
        DL_CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void phasedPublication() {
    Fixture f;
    NullLogger logger;
    auto source = std::make_unique<GatedSource>(f.pipeline());
    auto* gate = source.get();
    AppConfig config; config.mode = ExecutionMode::Real;
    auto result = CoreApplication::createProduction(config,logger,std::move(source));
    DL_CHECK(result); auto app = std::move(result.value());
    ProductionReadModel view(*app);
    DL_CHECK(view.startScan());
    awaitProof(*gate);
    DL_CHECK(view.refresh());
    const auto pending = app->productionSnapshotView().value();
    DL_CHECK(pending->phase == ProductionScanPhase::Ownership && pending->drives.size() == 1);
    DL_CHECK(pending->drives[0].status == DriveStatus::Unknown && pending->drives[0].proof_pending);
    DL_CHECK(productionStatusLabel(pending->drives[0]) == "SCANNING");
    DL_CHECK(!app->startProductionScan()); // Never overlap host snapshots.
    DL_CHECK(view.select(0));
    gate->release = true; app->waitForProductionScan();
    DL_CHECK(view.refresh() && view.selected()->status == DriveStatus::Ready);
    DL_CHECK(pending->drives[0].proof_pending && pending->ownership.physical_devices.empty());
    DL_CHECK(gate->inventories == 1 && gate->proofs == 1);
    gate->release = false; gate->entered = false;
    DL_CHECK(view.startScan()); awaitProof(*gate); DL_CHECK(view.refresh());
    DL_CHECK(view.selected() && view.selected()->proof_pending);
    DL_CHECK(view.selected()->status == DriveStatus::Unknown); // Previous READY is invalidated.
    app->cancelProductionScan();
    DL_CHECK(view.refresh() && view.snapshot().scan_error && view.snapshot().drives.empty());
    DL_CHECK(!view.selected());
    gate->release = true; gate->fail = true;
    DL_CHECK(!view.rescan() && view.snapshot().drives.empty());
    DL_CHECK(gate->inventories == 3 && gate->proofs == 3);
}
void deviceKindScope() {
    Fixture f;
    NullLogger logger;
    auto app = application(f,logger);
    ProductionReadModel view(*app);
    for (const auto kind : {PhysicalDeviceKind::Optical,PhysicalDeviceKind::Floppy,PhysicalDeviceKind::UnknownPhysical}) {
        auto& record = f.source.data.records.front();
        record.device_kind_observations.clear();
        if (kind == PhysicalDeviceKind::Optical) record.device_kind_observations = {{"sysfs:scsi/type","5"}};
        if (kind == PhysicalDeviceKind::Floppy) record.device_kind_observations = {{"udev:ID_DRIVE_FLOPPY","1"}};
        DL_CHECK(view.rescan());
        DL_CHECK(view.snapshot().drives.size() == 1 && view.snapshot().ownership.physical_devices.empty());
        const auto& row = view.snapshot().drives.front();
        DL_CHECK(row.observation.physical_kind == kind && row.status == DriveStatus::Unknown && !row.proof_pending);
        DL_CHECK(productionStatusLabel(row) == physicalDeviceKindName(kind));
        DL_CHECK(view.select(0) && view.overview().at(1).find("unavailable") != std::string::npos);
        DL_CHECK(f.fresh.reads == 0); // No ownership workflow when there are no disk candidates.
    }
    f.sync();
    auto pipeline = f.pipeline();
    const auto inventory = pipeline->scanInventory().value();
    const auto evidence = pipeline->readOwnership(inventory);
    const auto legacy = assessOwnership(inventory,evidence);
    const auto scoped = assessOwnership(inventory,evidence,PhysicalAssessmentScope::Disks);
    DL_CHECK(legacy == scoped); // Complete DISK results and all evidence are unchanged.
}

void boundedPresentation() {
    ProductionDrive drive;
    drive.observation.physical_kind = PhysicalDeviceKind::Disk;
    drive.status = DriveStatus::Unknown;
    for (int i = 0; i < 2000; ++i)
        drive.reasons.push_back({OwnershipReasonCode::IncompleteCoverage, "", "processes", "opaque " + std::to_string(i)});
    const auto summary = productionReasonSummary(drive);
    DL_CHECK(summary.size() == 1 && summary.front().find("2000") != std::string::npos);
    DL_CHECK(presentationText("disk\x1b[31m\n") == "disk\\x1B[31m\\x0A");
    DL_CHECK(productionStatusLabel(DriveStatus::Unknown) == "CAUTION");
    DL_CHECK(productionStatusLabel(DriveStatus::Protected) == "RESTRICTED");
}
}  // namespace

int main() {
    return test::run([] {
        pipelineAndModes();
        rescanAndClassification();
        gapsAndFailures();
        boundedPresentation();
        phasedPublication();
        deviceKindScope();
    });
}
