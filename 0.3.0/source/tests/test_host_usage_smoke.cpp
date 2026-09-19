#include "test_support.h"
#include "ownership_fixture.h"
#include <sstream>

// Exercise the actual formatter and entrypoint using metadata-only injected sources.
#define main drivelab_smoke_main
#include "../tools/linux_host_usage_smoke.cpp"
#undef main

namespace {
int scans = 0;
ownership_test::Fixture& fixture() {
    static ownership_test::Fixture f;
    static const bool initialized = [] {
        f.partition("sda1", "sda", {8, 1});
        f.add("sdb", {8, 16});
        f.io.lists["/proc/1/fd"].value = std::vector<std::string>{"3","4"};
        f.io.paths["/proc/1/fd/3"] = {drivelab::OwnershipPath{false,{8,1}, true, false, std::nullopt, false},false};
        f.io.paths["/proc/1/fd/4"] = {drivelab::OwnershipPath{false,{0,99},false, false, std::nullopt, false},false};
        f.io.lists["/sys/module/zfs"] = {std::vector<std::string>{}, false};
        return true;
    }();
    (void)initialized;
    return f;
}
}
namespace drivelab {
Result<LinuxInventorySourceSnapshot> UdevSysfsInventorySource::scan() {
    ++scans;
    LinuxBlockSourceRecord disk;
    disk.kernel_name = "sda"; disk.node_type = "disk"; disk.current_path = "/dev/sda";
    disk.major_number = "8"; disk.minor_number = "0"; disk.wwn = "50014ee001234567";
    disk.model_observations = {{"sysfs:device/model", "fixture\x1b[31m"}};
    LinuxBlockSourceRecord child;
    child.kernel_name = "sda1"; child.node_type = "partition"; child.current_path = "/dev/sda1";
    child.major_number = "8"; child.minor_number = "1";
    child.parent_kernel_name = "sda"; child.partition_number = "1";
    LinuxBlockSourceRecord other;
    other.kernel_name = "sdb"; other.node_type = "disk"; other.current_path = "/dev/sdb";
    other.major_number = "8"; other.minor_number = "16";
    return Result<LinuxInventorySourceSnapshot>::success({{disk, child, other}, {}});
}
HostRead<std::string> ProcHostFiles::read(HostTextFile file) {
    return {file == HostTextFile::MountInfo ?
        "1 1 8:1 / / rw - ext4 /dev/sda1 rw\n" : "Filename Type Size Used Priority\n", {}};
}
HostRead<HostPathStatus> ProcHostFiles::inspect(const std::string& p) {
    return {HostPathStatus{true, BlockDeviceNumber{8, p == "/dev/sda" ? 0U : p == "/dev/sdb" ? 16U : 1U}}, {}};
}
HostRead<SignatureSourceRecord> UdevSignatureSource::read(const BlockDeviceObservation&) {
    return {SignatureSourceRecord{{{"ID_FS_TYPE", "ext4"}, {"ID_FS_USAGE", "filesystem"}}}, {}};
}
OwnershipRead<std::string> NativeOwnershipIo::text(const std::string& p) { return fixture().io.text(p); }
OwnershipRead<std::vector<std::string>> NativeOwnershipIo::list(const std::string& p) { return fixture().io.list(p); }
OwnershipRead<std::string> NativeOwnershipIo::canonical(const std::string& p) { return fixture().io.canonical(p); }
OwnershipRead<OwnershipPath> NativeOwnershipIo::locate(const std::string& p) { return fixture().io.locate(p); }
FreshSignatures NativeFreshSignatureSource::read(const ResolvedInventorySnapshot&) {
    FreshSignatureEvidence e; e.state = FreshSignatureState::PermissionDenied;
    e.detail = "fixture unavailable\\x1b[31m";
    return {{"block:sdb",e}};
}
ZfsOwnershipRead NativeZfsOwnershipSource::read() { return {false, {}, "fixture unavailable\x1b[31m"}; }
}
namespace {
struct Capture {
    std::ostringstream output;
    std::streambuf* original = std::cout.rdbuf(output.rdbuf());
    ~Capture() { std::cout.rdbuf(original); }
};
}
int main() {
    return drivelab::test::run([] {
        std::string output;
        {
            Capture c;
            char name[] = "smoke", arg[] = "--ownership";
            char* args[] = {name, arg};
            DL_CHECK(drivelab_smoke_main(2, args) == 0);
            output = c.output.str();
        }
        DL_CHECK(scans == 1);
        DL_CHECK(output.find("fresh_signature node=\"block:sdb\" state=PermissionDenied") != std::string::npos);
        DL_CHECK(output.find("namespace_mount namespace=\"mnt:[1]\"") != std::string::npos);
        DL_CHECK(output.find("classification node=\"block:sda\" status=PROTECTED") != std::string::npos);
        DL_CHECK(output.find("coverage_gap source=\"processes\" scope=backing-chain  reason=IncompleteCoverage node=\"block:sda1\"") != std::string::npos);
        DL_CHECK(output.find("coverage_gap source=\"processes\" scope=global  reason=IncompleteCoverage node=\"\"") != std::string::npos);
        const auto last_drive = output.rfind("classification node=");
        const auto boundary = output.find("global_ownership_issues count=");
        DL_CHECK(last_drive != std::string::npos && boundary > last_drive && boundary != std::string::npos);
        DL_CHECK(output.substr(0, boundary).find("global_issue") == std::string::npos);
        DL_CHECK(output.substr(boundary).find("global_issue  reason=MissingSource") != std::string::npos);
        std::istringstream tail(output.substr(boundary));
        std::string line;
        std::getline(tail, line);
        while (std::getline(tail, line)) DL_CHECK(line.starts_with("global_issue  reason="));
        DL_CHECK(output.find('\x1b') == std::string::npos);
        DL_CHECK(output.find("model_source=\"sysfs:device/model\" value=\"fixture\\x1b[31m\"") != std::string::npos);
        {
            Capture c;
            char name[] = "smoke", arg[] = "--help"; char* args[] = {name, arg};
            DL_CHECK(drivelab_smoke_main(2, args) == 0 && scans == 1);
        }
    });
}
