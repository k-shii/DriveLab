#include "ownership_fixture.h"
using namespace ownership_test;
namespace {
void rawDisksAndStorage() {
    Fixture f; f.pve(); f.guest("name: fixture\nscsi0: /dev/sda,cache=none\n");
    auto a = f.assess();
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::GuestAssignment));
    f.io.texts["/etc/pve/nodes/pve/qemu-server/100.conf"].value = "scsi0: /dev/missing\n";
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);

    Fixture l; l.partition("sda3","sda",{8,3}); l.dm("dm-0",0,"sda3"); l.pve();
    l.io.texts["/etc/pve/storage.cfg"].value = "lvmthin: local-lvm\n\tvgname pve\n\tthinpool data\n\tnodes pve\n";
    l.guest("scsi0: local-lvm:vm-100-disk-0,size=16G\n");
    a = l.assess();
    DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ConfiguredStorage));
    DL_CHECK(reason(device(a), OwnershipReasonCode::GuestAssignment));
    DL_CHECK(trace(a, "pve-guest:qemu-server:100").complete);
    l.io.change = "/etc/pve/storage.cfg";
    l.io.reads.clear();
    DL_CHECK(device(l.assess()).status == DriveStatus::Unknown);
}
void pathsPciAndUnknown() {
    Fixture f; f.pve(); f.mount("sda","/");
    f.io.paths["/var/lib/vz"] = {OwnershipPath{false,{8,0}, true, false, std::nullopt, false}, false};
    f.io.texts["/etc/pve/storage.cfg"].value = "dir: local\n\tpath /var/lib/vz\n";
    f.guest("ide2: local:iso/test.iso,media=cdrom\n");
    auto a = f.assess();
    DL_CHECK(trace(a,"pve-storage:local").complete);
    DL_CHECK(reason(device(a), OwnershipReasonCode::ConfiguredStorage));

    Fixture p; p.pve(); p.guest("hostpci0: 00:01.0,pcie=1\n");
    p.io.lists["/sys/bus/pci/devices"] = {std::vector<std::string>{"0000:00:01.0"},false};
    p.io.links["/sys/bus/pci/devices/0000:00:01.0"] = {std::string("/sys/devices/pci0000:00/0000:00:01.0"),false};
    a = p.assess(); DL_CHECK(device(a).status == DriveStatus::Protected);
    DL_CHECK(reason(device(a), OwnershipReasonCode::GuestAssignment));
    p.io.links.clear();
    DL_CHECK(device(p.assess()).status != DriveStatus::Ready);
    Fixture u; u.pve(); u.guest("args: custom-storage\n");
    DL_CHECK(device(u.assess()).status == DriveStatus::Unknown);
    Fixture unsupported; unsupported.pve();
    unsupported.io.texts["/etc/pve/storage.cfg"].value = "custom: private\n\tsecret ignored\n";
    DL_CHECK(device(unsupported.assess()).status == DriveStatus::Unknown);
}
void zfsStorageAndMalformed() {
    Fixture f; f.pve();
    f.io.lists["/sys/module/zfs"] = {std::vector<std::string>{},false};
    f.zfs.result.pools.push_back({"rpool","42",{"root","root",{},{{"disk","disk","/dev/sda",{},true}},true},true});
    f.io.texts["/etc/pve/storage.cfg"].value = "zfspool: local-zfs\n\tpool rpool/data\n";
    f.guest("scsi0: local-zfs:vm-100-disk-0\n[snapshot]\nunused0: /dev/sda\n");
    auto a = f.assess();
    DL_CHECK(trace(a,"pve-guest:qemu-server:100").complete);
    DL_CHECK(device(a).status == DriveStatus::Protected);
    f.io.texts["/etc/pve/storage.cfg"].value = "zfspool: local-zfs\n\tpool rpool/data\n\tpool wrong\n";
    DL_CHECK(device(f.assess()).status == DriveStatus::Unknown);
}
}
int main() {
    return drivelab::test::run([] { rawDisksAndStorage(); pathsPciAndUnknown(); zfsStorageAndMalformed(); });
}
