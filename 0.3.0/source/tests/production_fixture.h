#pragma once

#include "ownership_fixture.h"
#include <memory>
#include "platform/linux/linux_production_source.h"

namespace production_test {
using namespace drivelab;

struct Raw final : LinuxInventorySource {
    LinuxInventorySourceSnapshot data;
    int scans = 0;
    bool fail = false;
    Result<LinuxInventorySourceSnapshot> scan() override {
        ++scans;
        if (fail) return Result<LinuxInventorySourceSnapshot>::failure({
            ErrorCode::IoError, "InjectedInventory", "Acquisition failed"});
        return Result<LinuxInventorySourceSnapshot>::success(data);
    }
};
struct Files final : LinuxHostFiles {
    ownership_test::Io& io;
    int reads = 0;
    explicit Files(ownership_test::Io& source) : io(source) {}
    HostRead<std::string> read(HostTextFile file) override {
        ++reads;
        return {io.text(file == HostTextFile::MountInfo ? "/proc/self/mountinfo" : "/proc/swaps").value, {}};
    }
    HostRead<HostPathStatus> inspect(const std::string& path) override {
        const auto found = io.locate(path);
        if (!found.value) return {std::nullopt, {}};
        return {HostPathStatus{found.value->block, found.value->device}, {}};
    }
};
struct Signatures final : LinuxSignatureSource {
    int reads = 0;
    HostRead<SignatureSourceRecord> read(const BlockDeviceObservation&) override {
        ++reads;
        return {SignatureSourceRecord{}, {}};
    }
};
struct Fresh final : FreshSignatureSource {
    int reads = 0;
    FreshSignatureState state = FreshSignatureState::None;
    FreshSignatures read(const ResolvedInventorySnapshot& inventory) override {
        ++reads;
        FreshSignatures result;
        for (const auto& node : inventory.nodes) {
            FreshSignatureEvidence item;
            item.state = state;
            item.detail = "injected fresh observation";
            result.emplace(blockOwnershipKey(node.observation.kernel_name), item);
        }
        return result;
    }
};

struct Fixture {
    ownership_test::Fixture host;
    Raw source;
    LinuxInventoryProvider raw{source};
    Files files{host.io};
    Signatures signatures;
    Fresh fresh;
    Fixture() { sync(); }
    void sync() {
        source.data = {};
        for (const auto& b : host.inventory.nodes) {
            const auto& observation = b.observation;
            LinuxBlockSourceRecord item;
            item.kernel_name = observation.kernel_name;
            item.current_path = observation.current_path;
            item.sysfs_path = observation.sysfs_path;
            item.node_type = observation.node_type == BlockNodeType::Partition ? "partition" : "disk";
            item.major_number = std::to_string(observation.device_number->major_number);
            item.minor_number = std::to_string(observation.device_number->minor_number);
            item.capacity_sectors_512 = std::to_string(observation.capacity_bytes.value_or(0) / 512);
            item.read_only = observation.read_only == true ? "1" : "0";
            item.device_kind_observations = {{"udev:ID_TYPE", "disk"}};
            item.model = "Integration Disk";
            item.serial = "SERIAL-" + std::to_string(observation.device_number->minor_number);
            item.wwn = observation.node_type == BlockNodeType::Disk &&
                       observation.observation_kind != BlockObservationKind::VirtualOrPseudo
                ? std::optional<std::string>("50014ee0012345" +
                    std::string(observation.device_number->minor_number < 10 ? "0" : "") +
                    std::to_string(observation.device_number->minor_number))
                : std::nullopt;
            item.parent_kernel_name = observation.parent_kernel_name;
            if (observation.partition_number) item.partition_number = std::to_string(*observation.partition_number);
            item.observation_kind = observation.observation_kind;
            source.data.records.push_back(std::move(item));
        }
    }
    std::unique_ptr<ProductionInventorySource> pipeline() {
        return std::make_unique<LinuxProductionSource>(
            raw, files, signatures, host.io, host.zfs, fresh);
    }
};
}  // namespace production_test
