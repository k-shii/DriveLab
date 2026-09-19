#include "platform/linux/linux_host_usage.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace drivelab {
namespace {

DiscoveryIssue failure(const std::string& field, int error) {
    return {error == ENOENT || error == ENODEV || error == ENXIO
                ? DiscoveryIssueCode::Disappeared : DiscoveryIssueCode::ReadFailure,
            "", field, "Native metadata read/stat failed", error};
}

}  // namespace

HostRead<std::string> ProcHostFiles::read(HostTextFile file) {
    const char* path = file == HostTextFile::MountInfo ? "/proc/self/mountinfo" : "/proc/swaps";
    const char* field = file == HostTextFile::MountInfo ? "mountinfo" : "swaps";
    HostRead<std::string> result;
    // Only these fixed proc metadata files may be opened. No /dev node is opened.
    const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) { result.issues.push_back(failure(field, errno)); return result; }
    std::string content;
    char buffer[4096];
    bool complete = true;
    for (;;) {
        const auto count = ::read(descriptor, buffer, sizeof(buffer));
        if (count < 0) {
            const int error = errno;
            if (error == EINTR) continue;
            result.issues.push_back(failure(field, error)); complete = false; break;
        }
        if (count == 0) break;
        content.append(buffer, static_cast<std::size_t>(count));
        if (content.size() > 16U * 1024U * 1024U) {
            result.issues.push_back({DiscoveryIssueCode::ReadFailure, "", field,
                                    "Proc metadata exceeds 16 MiB acquisition bound"});
            complete = false; break;
        }
    }
    if (::close(descriptor) != 0) {
        result.issues.push_back(failure(field, errno)); complete = false;
    }
    // A partial record set must never be mistaken for complete host evidence.
    if (complete) result.value = std::move(content);
    return result;
}

HostRead<HostPathStatus> ProcHostFiles::inspect(const std::string& path) {
    HostRead<HostPathStatus> result;
    if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos) {
        result.issues.push_back({DiscoveryIssueCode::MalformedEvidence, "", path,
                                "Expected absolute path without NUL"});
        return result;
    }
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0) {
        result.issues.push_back(failure(path, errno)); return result;
    }
    HostPathStatus value;
    value.is_block = S_ISBLK(status.st_mode);
    if (value.is_block) {
        value.device_number = BlockDeviceNumber{
            static_cast<std::uint32_t>(major(status.st_rdev)),
            static_cast<std::uint32_t>(minor(status.st_rdev))};
    }
    result.value = value;
    return result;
}

}  // namespace drivelab
