#pragma once

// Deliberately not a system header or C ABI: standard-library calls cannot
// bind to these fixture functions. Only the ownership adapter opts into them.
#include <cstddef>
#include <cstdint>

namespace drivelab::ownership_io {
struct Directory;
struct DirectoryEntry { char d_name[256]; };
struct Status { unsigned st_mode = 0; std::uint64_t st_rdev = 0, st_dev = 0; };

inline constexpr int read_flags = 02000000 | 04000;
inline bool is_directory(const Status& status) { return (status.st_mode & 0170000) == 0040000; }
inline bool is_regular(const Status& status) { return (status.st_mode & 0170000) == 0100000; }
inline bool is_block(const Status& status) { return (status.st_mode & 0170000) == 0060000; }
inline bool is_character(const Status& status) { return (status.st_mode & 0170000) == 0020000; }
inline bool is_socket(const Status& status) { return (status.st_mode & 0170000) == 0140000; }
inline unsigned device_major(std::uint64_t device) { return static_cast<unsigned>(device >> 32); }
inline unsigned device_minor(std::uint64_t device) { return static_cast<unsigned>(device & 0xffffffff); }

inline bool is_nonstorage(const Status& s) {
    const auto type = s.st_mode & 0170000;
    if (type == 0010000) return true; // Sockets can retain SCM_RIGHTS file references.
    if (type != 0020000) return false;
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
int open(const char*, int, ...);
int close(int);
std::ptrdiff_t read(int, void*, std::size_t);
std::ptrdiff_t readlink(const char*, char*, std::size_t);
int stat(const char*, Status*);
int fstat(int, Status*);
Directory* opendir(const char*);
DirectoryEntry* readdir(Directory*);
int dirfd(Directory*);
int closedir(Directory*);
} // namespace drivelab::ownership_io
