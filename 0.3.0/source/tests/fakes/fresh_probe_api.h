#pragma once
#include "fakes/ownership_io_api.h"
namespace drivelab::fresh_probe_api {
using namespace ownership_io;
struct Probe;
struct Partition;
using blkid_probe = Probe*;
blkid_probe blkid_new_probe();
void blkid_free_probe(blkid_probe);
int blkid_probe_set_device(blkid_probe, int, std::int64_t, std::int64_t);
std::int64_t blkid_probe_get_size(blkid_probe);
int blkid_probe_enable_superblocks(blkid_probe, int);
int blkid_probe_set_superblocks_flags(blkid_probe, int);
int blkid_probe_enable_partitions(blkid_probe, int);
int blkid_probe_enable_topology(blkid_probe, int);
int blkid_do_safeprobe(blkid_probe);
int blkid_probe_lookup_value(blkid_probe, const char*, const char**, std::size_t*);
int blkid_known_fstype(const char*);
Probe* blkid_probe_get_partitions(blkid_probe);
int blkid_partlist_numof_partitions(Probe*);
Partition* blkid_partlist_get_partition(Probe*, int);
int blkid_partition_get_partno(Partition*);
std::int64_t blkid_partition_get_start(Partition*);
std::int64_t blkid_partition_get_size(Partition*);
int blkid_partition_get_type(Partition*);
const char* blkid_partition_get_type_string(Partition*);
unsigned long long blkid_partition_get_flags(Partition*);
int blkid_partition_is_extended(Partition*);
int blkid_partition_is_logical(Partition*);
std::ptrdiff_t read_at(int, void*, std::size_t, std::uint64_t);
inline constexpr int signature_flags = 32 | 128;
}
