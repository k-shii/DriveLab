#include "platform/linux/linux_ownership_source.h"
#include "core/scan_profile.h"
#include "core/scan_control.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <memory>

#ifdef DRIVELAB_TEST_OWNERSHIP_IO
#include "fakes/ownership_io_api.h"
#else
#include "platform/linux/ownership_io_api.h"
#endif

namespace drivelab {
namespace io = ownership_io;
namespace {
struct DirectoryDeleter {
    void operator()(io::Directory* directory) const noexcept {
        (void)io::closedir(directory);
    }
};
bool absent(int error) { return error == ENOENT || error == ENOTDIR; }
bool metadataPath(const std::string& path) {
    // Public interface cannot be used to read a block device or arbitrary data.
    return path.starts_with("/sys/") || path.starts_with("/proc/") ||
           path.starts_with("/etc/pve/");
}
}
OwnershipRead<std::string> NativeOwnershipIo::text(const std::string& path) {
    checkScanCancelled();
    ScanStage timing(path.starts_with("/proc/") ? "io.proc_text" : "io.other_text");
    if (!metadataPath(path)) return {};
    const int fd = io::open(path.c_str(), io::read_flags);
    if (fd < 0) return {std::nullopt, absent(errno)};
    io::Status status {};
    if (io::fstat(fd, &status) != 0 || !io::is_regular(status)) { io::close(fd); return {}; }
    std::string result;
    char buffer[4096];
    bool ok = true;
    for (;;) {
        const auto count = io::read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = false; break; }
        if (!count) break;
        result.append(buffer, static_cast<std::size_t>(count));
        if (result.size() > 4 * 1024 * 1024) { ok = false; break; }
    }
    if (io::close(fd) != 0) ok = false;
    if (!ok) return {};
    return {result, false};
}
OwnershipRead<std::vector<std::string>> NativeOwnershipIo::list(const std::string& path) {
    checkScanCancelled();
    ScanStage timing(path.starts_with("/proc") ? "io.proc_list" : "io.other_list");
    // Our enumeration descriptor is visible in our own /proc fd list.
    // Exclude only that owned descriptor while open, never arbitrary vanished fds.
    bool self_fds = path == "/proc/self/fd", self_thread_fds = false;
    if (!self_fds && path.starts_with("/proc/") && path.ends_with("/fd")) {
        // getpid() can use a different PID namespace than the mounted procfs.
        char self[32];
        const auto length = io::readlink("/proc/self", self, sizeof(self));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(self)) return {};
        const std::string pid(self, static_cast<std::size_t>(length));
        if (pid.find_first_not_of("0123456789") != std::string::npos) return {};
        self_fds = path == "/proc/" + pid + "/fd";
        const auto prefix = "/proc/" + pid + "/task/";
        if (path.starts_with(prefix)) {
            const auto tid = path.substr(prefix.size(),path.size()-prefix.size()-3);
            self_thread_fds = !tid.empty() && tid.find_first_not_of("0123456789") == std::string::npos;
        }
    }
    if (self_fds || self_thread_fds) {
        std::unique_ptr<io::Directory, DirectoryDeleter> directory(io::opendir(path.c_str()));
        if (!directory) return {std::nullopt, absent(errno)};
        const int observer_fd = io::dirfd(directory.get());
        if (observer_fd < 0) return {};
        std::vector<std::string> names;
        for (;;) {
            errno = 0;
            const auto* entry = io::readdir(directory.get());
            if (!entry) { if (errno != 0) return {}; break; }
            const std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            if (name == std::to_string(observer_fd)) {
                if (self_fds) continue;
                // A thread may have unshared its fd table. Exclude this number
                // only if that table actually exposes our proc-directory handle.
                const auto link = path + "/" + name;
                std::vector<char> target(path.size()+1);
                const auto count = io::readlink(link.c_str(),target.data(),target.size());
                if (count > 0 && static_cast<std::size_t>(count) == path.size() &&
                    std::string(target.data(),static_cast<std::size_t>(count)) == path) continue;
            }
            if (names.size() >= 131072) return {};
            names.push_back(name);
        }
        if (io::closedir(directory.release()) != 0) return {};
        std::sort(names.begin(), names.end());
        return {names, false};
    }
    std::error_code ec;
    std::filesystem::directory_iterator it(path, ec), end;
    if (ec) return {std::nullopt, absent(ec.value())};
    std::vector<std::string> names;
    for (; it != end; it.increment(ec)) {
        if (ec || names.size() >= 131072) return {};
        names.push_back(it->path().filename().string());
    }
    if (ec) return {};
    std::sort(names.begin(), names.end());
    return {names, false};
}
OwnershipRead<std::string> NativeOwnershipIo::canonical(const std::string& path) {
    checkScanCancelled();
    ScanStage timing("io.canonical");
    std::error_code ec;
    // Namespace links are deliberately non-filesystem targets (mnt:[...]).
    if (path.starts_with("/proc/") && (path.ends_with("/ns/mnt") || path.find("/fd/") != std::string::npos)) {
        const auto target = std::filesystem::read_symlink(path, ec);
        if (ec) return {std::nullopt, absent(ec.value())};
        return {target.string(), false};
    }
    const auto result = std::filesystem::canonical(path, ec);
    if (ec) return {std::nullopt, absent(ec.value())};
    return {result.string(), false};
}
OwnershipRead<OwnershipPath> NativeOwnershipIo::locate(const std::string& path) {
    checkScanCancelled();
    ScanStage timing("io.locate");
    io::Status status {};
    if (io::stat(path.c_str(), &status) != 0) return {std::nullopt, absent(errno)};
    const bool block = io::is_block(status);
    const auto number = block ? status.st_rdev : status.st_dev;
    return {OwnershipPath{block, {static_cast<std::uint32_t>(io::device_major(number)),
                                 static_cast<std::uint32_t>(io::device_minor(number))},
                         io::is_regular(status) || io::is_directory(status), io::is_nonstorage(status),
                         io::is_character(status) ? std::optional<BlockDeviceNumber>{{
                             static_cast<std::uint32_t>(io::device_major(status.st_rdev)),
                             static_cast<std::uint32_t>(io::device_minor(status.st_rdev))}} : std::nullopt,
                         io::is_socket(status)}, false};
}
} // namespace drivelab
