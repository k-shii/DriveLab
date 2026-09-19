#pragma once
#include "platform/linux/ownership_io_api.h"
#include <blkid/blkid.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace drivelab::fresh_probe_api {
using namespace ownership_io;
using ::blkid_probe;
using ::blkid_new_probe;
using ::blkid_free_probe;
using ::blkid_probe_set_device;
using ::blkid_probe_get_size;
using ::blkid_probe_enable_superblocks;
using ::blkid_probe_set_superblocks_flags;
using ::blkid_probe_enable_partitions;
using ::blkid_probe_enable_topology;
using ::blkid_do_safeprobe;
using ::blkid_probe_lookup_value;
using ::blkid_known_fstype;
using ::blkid_probe_get_partitions;
using ::blkid_partlist_numof_partitions;
using ::blkid_partlist_get_partition;
using ::blkid_partition_get_partno;
using ::blkid_partition_get_start;
using ::blkid_partition_get_size;
using ::blkid_partition_get_type;
using ::blkid_partition_get_type_string;
using ::blkid_partition_get_flags;
using ::blkid_partition_is_extended;
using ::blkid_partition_is_logical;
inline std::ptrdiff_t read_at(int fd, void* buffer, std::size_t size, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) { errno = EOVERFLOW; return -1; }
    return ::pread(fd, buffer, size, static_cast<off_t>(offset));
}
inline constexpr int signature_flags = BLKID_SUBLKS_TYPE | BLKID_SUBLKS_USAGE;
}
