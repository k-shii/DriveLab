#pragma once

// Private compile-time boundary for native ownership metadata calls.
// Production always uses the system POSIX declarations and implementations.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace drivelab::ownership_io {
using Directory = ::DIR;
using DirectoryEntry = ::dirent;
using Status = struct ::stat;
using ::open;
using ::close;
using ::read;
using ::readlink;
using ::stat;
using ::fstat;
using ::opendir;
using ::readdir;
using ::dirfd;
using ::closedir;

inline constexpr int read_flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
inline bool is_directory(const Status& status) { return S_ISDIR(status.st_mode); }
inline bool is_regular(const Status& status) { return S_ISREG(status.st_mode); }
inline bool is_block(const Status& status) { return S_ISBLK(status.st_mode); }
inline bool is_character(const Status& status) { return S_ISCHR(status.st_mode); }
inline bool is_socket(const Status& status) { return S_ISSOCK(status.st_mode); }
inline unsigned device_major(dev_t device) { return major(device); }
inline unsigned device_minor(dev_t device) { return minor(device); }
inline bool is_nonstorage(const Status& s) {
    if (S_ISFIFO(s.st_mode)) return true; // Sockets can retain SCM_RIGHTS file references.
    if (!S_ISCHR(s.st_mode)) return false;
    const auto major_number = device_major(s.st_rdev), minor_number = device_minor(s.st_rdev);
    if (major_number == 1)
        return minor_number == 3 || minor_number == 5 || minor_number == 7 || minor_number == 8 || minor_number == 9 ||
               minor_number == 11; // printk ring buffer (kmsg), not an open block file.
    // Fixed Linux ABI identities: watchdog, tun, vhost-net, rfkill. These
    // expose reset control, packet queues or radio switches, not block files.
    // In particular this does not admit other misc/vhost or USB devices.
    // Factory/control descriptors do not retain their derived objects:
    // virt/kvm/kvm_main.c returns VM state in a separate anonymous FD;
    // fs/autofs/dev-ioctl.c operates on separately supplied mount FDs;
    // drivers/md/dm-ioctl.c retains only an event counter per control FD.
    // DM backing, including staged tables, stays represented by the independently
    // revalidated holder graph (drivers/md/dm.c open_table_device).
    if (major_number == 10)
        return minor_number == 130 || minor_number == 200 || minor_number == 232 || minor_number == 235 ||
               minor_number == 236 || minor_number == 238 || minor_number == 242;
    // Only the fixed evdev event range; unreviewed/dynamic input minors fail closed.
    if (major_number == 13) return minor_number >= 64 && minor_number <= 95;
    return major_number == 2 || major_number == 3 || major_number == 4 ||
        (major_number == 5 && minor_number <= 3) || (major_number >= 128 && major_number <= 143);
}
} // namespace drivelab::ownership_io
