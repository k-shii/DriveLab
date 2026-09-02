#pragma once

#include "core/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace drivelab {

struct DriveId {
    std::string value;

    [[nodiscard]] bool empty() const { return value.empty(); }

    friend bool operator==(const DriveId&, const DriveId&) = default;
    friend bool operator<(const DriveId& left, const DriveId& right) {
        return left.value < right.value;
    }
};

enum class MediaKind {
    Unknown,
    Hdd,
    Ssd,
    Nvme
};

enum class DriveStatus {
    Ready,
    Busy,
    Protected,
    Degraded,
    Failing,
    Offline,
    Unknown
};

struct DriveIdentity {
    std::string serial;
    std::string wwn;
    std::string model;
    std::string topology;
    std::uint64_t capacity_bytes = 0;

    [[nodiscard]] Result<DriveId> driveId() const;
    [[nodiscard]] Result<std::string> lockKey() const;
    [[nodiscard]] bool samePhysicalDevice(const DriveIdentity& other) const;
};

struct Drive {
    DriveIdentity identity;
    std::string current_path;
    MediaKind media = MediaKind::Unknown;
    DriveStatus status = DriveStatus::Unknown;
    std::vector<std::string> protection_reasons;

    [[nodiscard]] bool isProtected() const;
};

std::string mediaKindName(MediaKind kind);
std::string driveStatusName(DriveStatus status);

}  // namespace drivelab
