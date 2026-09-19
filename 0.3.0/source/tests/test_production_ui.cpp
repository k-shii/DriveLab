#include "production_fixture.h"
#include "app/native_application.h"
#include "ui/production_ui.h"
#include "core/scan_control.h"
#include <ncurses.h>
#include <sstream>
#include <thread>
#include <chrono>

// Exercise the executable's actual dispatch/bootstrap with this private terminal.
#define main drivelab_test_entry
#include "../prototype/drivelab.cpp"
#undef main

using namespace drivelab;
namespace {
production_test::Fixture* native_fixture = nullptr;
}

// Run the actual native application composition against injected platform APIs.
// No real adapter is linked into this executable.
namespace drivelab {
Result<LinuxInventorySourceSnapshot> UdevSysfsInventorySource::scan() { return native_fixture->source.scan(); }
HostRead<std::string> ProcHostFiles::read(HostTextFile file) { return native_fixture->files.read(file); }
HostRead<HostPathStatus> ProcHostFiles::inspect(const std::string& path) { return native_fixture->files.inspect(path); }
HostRead<SignatureSourceRecord> UdevSignatureSource::read(const BlockDeviceObservation& node) { return native_fixture->signatures.read(node); }
OwnershipRead<std::string> NativeOwnershipIo::text(const std::string& path) { return native_fixture->host.io.text(path); }
OwnershipRead<std::vector<std::string>> NativeOwnershipIo::list(const std::string& path) { return native_fixture->host.io.list(path); }
OwnershipRead<std::string> NativeOwnershipIo::canonical(const std::string& path) { return native_fixture->host.io.canonical(path); }
OwnershipRead<OwnershipPath> NativeOwnershipIo::locate(const std::string& path) { return native_fixture->host.io.locate(path); }
FreshSignatures NativeFreshSignatureSource::read(const ResolvedInventorySnapshot& inventory) { return native_fixture->fresh.read(inventory); }
ZfsOwnershipRead NativeZfsOwnershipSource::read() { return native_fixture->host.zfs.read(); }
}

namespace {
void expect(const std::string& text) {
    if (fake_tui::text().find(text)==std::string::npos)
        throw std::runtime_error("Missing terminal text: "+text+"\n"+fake_tui::text());
}
void waitFor(const std::function<bool()>& ready) {
    fake_tui::inputs.push_back([ready, deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10)] {
        if (ready()) return KEY_RESIZE;
        DL_CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ERR;
    });
}
void settled(CoreApplication& app) {
    waitFor([&app] {
        const auto phase = app.productionSnapshotView().value()->phase;
        return phase == ProductionScanPhase::Complete || phase == ProductionScanPhase::Failed;
    });
}
void exitKeys() { fake_tui::key('q'); fake_tui::key(KEY_LEFT); fake_tui::key('\n'); }

void executableModes() {
    production_test::Fixture f;
    native_fixture=&f;
    std::ostringstream output, errors;
    auto* original_out=std::cout.rdbuf(output.rdbuf());
    auto* original_err=std::cerr.rdbuf(errors.rdbuf());
    struct RestoreStreams {
        std::streambuf* out;
        std::streambuf* err;
        ~RestoreStreams() { std::cout.rdbuf(out); std::cerr.rdbuf(err); }
    } restore{original_out,original_err};
    char name[]="drivelab";
    for (std::string flag : {"--version","--help","--dry-run"}) {
        char* argv[]={name,flag.data()};
        DL_CHECK(drivelab_test_entry(2,argv)==0);
    }
    DL_CHECK(f.source.scans==0);
    DL_CHECK(output.str().find("No device or storage command was executed")!=std::string::npos);

    fake_tui::reset();
    fake_tui::inputs.push_back([] {
        expect("simulator"); expect("PASSED");
        return 'R';
    });
    exitKeys();
    char demo[]="--demo";
    char* demo_args[]={name,demo};
    DL_CHECK(drivelab_test_entry(2,demo_args)==0);
    DL_CHECK(f.source.scans==0 && fake_tui::inputs.empty());

    fake_tui::reset();
    waitFor([] { return fake_tui::text().find("READY") != std::string::npos; });
    fake_tui::inputs.push_back([] { expect("DriveLab // live"); expect("READY"); return 'J'; });
    exitKeys();
    char* live_args[]={name};
    DL_CHECK(drivelab_test_entry(1,live_args)==0);
    DL_CHECK(f.source.scans==1 && fake_tui::inputs.empty());
}

void liveScreen() {
    production_test::Fixture f;
    native_fixture=&f;
    NullLogger logger;
    auto app=createNativeApplication(logger);
    DL_CHECK(app && f.source.scans==0);
    fake_tui::reset();
    settled(*app.value());
    fake_tui::inputs.push_back([] {
        expect("DriveLab // live"); expect("READY"); expect("Integration Disk");
        DL_CHECK(fake_tui::text().find("PASSED")==std::string::npos);
        DL_CHECK(fake_tui::text().find("Temperature")==std::string::npos);
        return KEY_DOWN;
    });
    fake_tui::key('\n'); // Select drive, enter tabs.
    fake_tui::inputs.push_back([] { expect("SERIAL-0"); return 'D'; });
    fake_tui::key(KEY_NPAGE);
    fake_tui::inputs.push_back([&] {
        f.fresh.state=FreshSignatureState::PermissionDenied;
        return 'R';
    });
    settled(*app.value());
    fake_tui::inputs.push_back([] { expect("CAUTION"); return 'q'; });
    fake_tui::key('\n'); // Cancel is the default.
    fake_tui::inputs.push_back([&] {
        expect("CAUTION");
        f.source.data.records.clear();
        f.host.io.lists["/sys/class/block"].value=std::vector<std::string>{};
        return 'R';
    });
    settled(*app.value());
    fake_tui::inputs.push_back([] { expect("No physical drives"); expect("Selection cleared"); return 'J'; });
    fake_tui::inputs.push_back([&] {
        expect("No jobs."); f.source.fail=true; return 'R';
    });
    settled(*app.value());
    fake_tui::inputs.push_back([] { expect("Scan unavailable"); expect("Scan failed"); return 'E'; });
    exitKeys();
    DL_CHECK(tui::runProductionUi(*app.value())==0);
    DL_CHECK(f.source.scans==4 && fake_tui::inputs.empty());
    DL_CHECK(app.value()->processRunner().planned().empty());
}


class PendingOwnership final : public ProductionInventorySource {
public:
    explicit PendingOwnership(production_test::Fixture& f) : source_(f.pipeline()) {}
    Result<ResolvedInventorySnapshot> scanInventory() override { return source_->scanInventory(); }
    OwnershipEvidence readOwnership(const ResolvedInventorySnapshot&) override {
        for (;;) {
            checkScanCancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
private:
    std::unique_ptr<ProductionInventorySource> source_;
};
void pendingScreenAndExit() {
    production_test::Fixture f;
    f.host.add("media-device",{11,0});
    f.sync();
    for (auto& record : f.source.data.records)
        if (record.kernel_name == "media-device") record.device_kind_observations = {{"sysfs:scsi/type","5"}};
    NullLogger logger;
    AppConfig config; config.mode = ExecutionMode::Real;
    auto app=CoreApplication::createProduction(config,logger,std::make_unique<PendingOwnership>(f));
    DL_CHECK(app);
    fake_tui::reset();
    waitFor([] { return fake_tui::text().find("SCANNING") != std::string::npos; });
    fake_tui::inputs.push_back([] {
        expect("SCANNING"); expect("OPTICAL");
        DL_CHECK(fake_tui::text().find("READY")==std::string::npos);
        return KEY_DOWN;
    });
    fake_tui::key(KEY_DOWN); // The optical row sorts first; inspect the pending disk.
    fake_tui::key('\n');
    fake_tui::inputs.push_back([] { expect("proof is incomplete"); return 'q'; });
    fake_tui::key(KEY_LEFT); fake_tui::key('\n');
    DL_CHECK(tui::runProductionUi(*app.value())==0);
    DL_CHECK(app.value()->productionSnapshotView().value()->phase == ProductionScanPhase::Failed);
}

void layoutAndMouse() {
    for (const auto size : {std::pair{24,70},std::pair{34,100},std::pair{45,160}}) {
        production_test::Fixture f;
        native_fixture=&f;
        f.host.io.texts["/proc/self/mountinfo"].value=
            "1 0 8:0 / / rw - ext4 /dev/sda rw\n2 1 0:3 / /proc rw - proc proc rw\n";
        NullLogger logger;
        auto app=createNativeApplication(logger); DL_CHECK(app);
        fake_tui::reset(size.first,size.second);
        settled(*app.value());
        fake_tui::inputs.push_back([] { expect("RESTRICTED"); return KEY_DOWN; });
        fake_tui::key('\n');
        fake_tui::key('\n');
        fake_tui::inputs.push_back([] { expect("running system root"); return 27; });
        // Mouse release with no matching press must not activate Scan.
        fake_tui::click(4,23,BUTTON1_RELEASED);
        fake_tui::inputs.push_back([&] { DL_CHECK(f.source.scans==1); return KEY_RESIZE; });
        exitKeys();
        DL_CHECK(tui::runProductionUi(*app.value())==0);
    }
    production_test::Fixture f;
    native_fixture=&f;
    NullLogger logger;
    auto app=createNativeApplication(logger); DL_CHECK(app);
    fake_tui::reset(34,100);
    settled(*app.value());
    // Shared geometry places Scan at x=20..29.
    fake_tui::click(4,23,BUTTON1_PRESSED);
    fake_tui::click(4,23,BUTTON1_RELEASED);
    fake_tui::click(4,23,BUTTON1_RELEASED); // Duplicate release.
    settled(*app.value());
    fake_tui::inputs.push_back([&] { DL_CHECK(f.source.scans==2); return KEY_RESIZE; });
    fake_tui::inputs.push_back([] {
        terminal_window={12,45};
        return KEY_RESIZE;
    });
    fake_tui::inputs.push_back([] { expect("resize"); return 'q'; });
    fake_tui::key(KEY_LEFT); fake_tui::key('\n');
    DL_CHECK(tui::runProductionUi(*app.value())==0);
}

}  // namespace

int main() {
    return test::run([] { executableModes(); liveScreen(); layoutAndMouse(); pendingScreenAndExit(); });
}
