#include "core/drive.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace drivelab {
namespace {

std::string canonical(std::string value) {
    auto not_space = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

}  // namespace

Result<DriveId> DriveIdentity::driveId() const {
    const std::string normalized_wwn = canonical(wwn);
    if (!normalized_wwn.empty()) {
        return Result<DriveId>::success({"wwn:" + normalized_wwn});
    }

    const std::string normalized_serial = canonical(serial);
    const std::string normalized_model = canonical(model);
    if (normalized_serial.empty() || normalized_model.empty() || capacity_bytes == 0) {
        return Result<DriveId>::failure({
            ErrorCode::InvalidIdentity,
            "DriveIdentity",
            "A stable drive identity requires WWN or serial plus model and capacity"
        });
    }

    std::ostringstream key;
    key << "serial:" << normalized_serial
        << "|model:" << normalized_model
        << "|bytes:" << capacity_bytes;
    return Result<DriveId>::success({key.str()});
}

Result<std::string> DriveIdentity::lockKey() const {
    Result<DriveId> id = driveId();
    if (!id) return Result<std::string>::failure(id.error());
    return Result<std::string>::success(id.value().value);
}

bool DriveIdentity::samePhysicalDevice(const DriveIdentity& other) const {
    Result<DriveId> left = driveId();
    Result<DriveId> right = other.driveId();
    if (!left || !right || left.value() != right.value()) return false;

    auto differsWhenKnown = [](const std::string& first, const std::string& second) {
        return !canonical(first).empty() && !canonical(second).empty() &&
               canonical(first) != canonical(second);
    };
    if (differsWhenKnown(serial, other.serial) || differsWhenKnown(model, other.model)) {
        return false;
    }
    return capacity_bytes == 0 || other.capacity_bytes == 0 ||
           capacity_bytes == other.capacity_bytes;
}

bool Drive::isProtected() const {
    return status == DriveStatus::Protected || !protection_reasons.empty();
}

std::string mediaKindName(MediaKind kind) {
    switch (kind) {
        case MediaKind::Unknown: return "UNKNOWN";
        case MediaKind::Hdd: return "HDD";
        case MediaKind::Ssd: return "SSD";
        case MediaKind::Nvme: return "NVMe";
    }
    return "UNKNOWN";
}

std::string driveStatusName(DriveStatus status) {
    switch (status) {
        case DriveStatus::Ready: return "READY";
        case DriveStatus::Busy: return "BUSY";
        case DriveStatus::Protected: return "PROTECTED";
        case DriveStatus::Degraded: return "DEGRADED";
        case DriveStatus::Failing: return "FAILING";
        case DriveStatus::Offline: return "OFFLINE";
        case DriveStatus::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace drivelab
