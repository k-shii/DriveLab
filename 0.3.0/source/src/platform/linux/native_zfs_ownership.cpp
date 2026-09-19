#include "platform/linux/linux_ownership_source.h"

#ifdef DRIVELAB_HAVE_ZFS_OWNERSHIP
#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <libzfs.h>
#include <libnvpair.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>

namespace drivelab {
namespace {
ZfsVdevObservation decodeVdev(nvlist_t* list, const std::string& position,
                               std::size_t depth, std::size_t& count) {
    ZfsVdevObservation out;
    out.key = position;
    if (!list || ++count > 8192 || depth > 64) { out.complete = false; return out; }
    const char* type = nullptr;
    std::uint64_t guid = 0;
    if (nvlist_lookup_string(list, "type", &type) != 0 || !type) out.complete = false;
    else out.type = type;
    if (nvlist_lookup_uint64(list, "guid", &guid) == 0)
        out.key += ":" + std::to_string(guid);
    else out.complete = false;
    const char* path = nullptr;
    if (nvlist_exists(list, "path")) {
        if (nvlist_lookup_string(list, "path", &path) == 0 && path && path[0] == '/') out.path = path;
        else out.complete = false;
    }
    for (const auto* field : {"children", "spares", "l2cache"}) {
        if (!nvlist_exists(list, field)) continue;
        nvlist_t** children = nullptr;
        unsigned int size = 0;
        if (nvlist_lookup_nvlist_array(list, field, &children, &size) != 0 ||
            size > 8192 || (size && !children)) { out.complete = false; continue; }
        for (unsigned int i = 0; i < size; ++i)
            out.children.push_back(decodeVdev(children[i], position + "/" + field + "/" +
                                              std::to_string(i), depth + 1, count));
    }
    return out;
}
struct ControlGuard {
    int value;
    ~ControlGuard() { if (value >= 0) ::close(value); }
};
struct Library {
    libzfs_handle_t* value;
    ~Library() { if (value) libzfs_fini(value); }
};
struct Pool {
    zpool_handle_t* value;
    ~Pool() { if (value) zpool_close(value); }
};
bool environmentPresent(const char* name) {
#ifdef _MSC_VER
    // The adapter is also compiled against an isolated public-API fake on MSVC.
    char* value = nullptr;
    std::size_t size = 0;
    const int error = _dupenv_s(&value, &size, name);
    const bool present = error != 0 || value != nullptr;
    std::free(value);
    return present;
#else
    return std::getenv(name) != nullptr;
#endif
}
int collectPool(zpool_handle_t* handle, void* data) noexcept {
    Pool owned{handle}; // libzfs transfers each iterated handle to the callback.
    auto& out = *static_cast<ZfsOwnershipRead*>(data);
    try {
        if (!handle || out.pools.size() >= 4096) {
            out.complete = false;
            return 1;
        }
        const auto* name = zpool_get_name(handle);
        auto* config = zpool_get_config(handle, nullptr); // borrowed until close
        nvlist_t* tree = nullptr;
        std::uint64_t guid = 0;
        if (!name || !*name || !config ||
            nvlist_lookup_uint64(config, "pool_guid", &guid) != 0 || guid == 0 ||
            nvlist_lookup_nvlist(config, "vdev_tree", &tree) != 0) {
            out.complete = false;
            return 0; // Retain other observable pools, without claiming coverage.
        }
        ZfsPoolObservation pool;
        pool.name = name;
        pool.guid = std::to_string(guid);
        pool.complete = zpool_get_state(handle) == POOL_STATE_ACTIVE;
        if (!pool.complete) out.complete = false;
        std::size_t count = 0;
        pool.root = decodeVdev(tree, "root", 0, count);
        out.pools.push_back(std::move(pool));
        return 0;
    } catch (...) {
        // Never unwind through the C iterator; RAII still closes this pool.
        out.complete = false;
        return 1;
    }
}
ZfsOwnershipRead capture() {
    ZfsOwnershipRead out;
    Library library{libzfs_init()};
    if (!library.value) {
        out.detail = "Public libzfs initialization failed";
        return out;
    }
    libzfs_print_on_error(library.value, B_FALSE);
    out.complete = true;
    if (zpool_iter(library.value, collectPool, &out) != 0 ||
        libzfs_errno(library.value) != EZFS_SUCCESS) out.complete = false;
    std::sort(out.pools.begin(), out.pools.end(), [](const auto& a, const auto& b) {
        return std::tie(a.name, a.guid) < std::tie(b.name, b.guid);
    });
    if (!out.complete) out.detail = "Incomplete public libzfs pool configuration";
    return out;
}
bool sameVdev(const ZfsVdevObservation& a, const ZfsVdevObservation& b) {
    if (std::tie(a.key, a.type, a.path, a.complete) !=
        std::tie(b.key, b.type, b.path, b.complete) || a.children.size() != b.children.size())
        return false;
    for (std::size_t i = 0; i < a.children.size(); ++i)
        if (!sameVdev(a.children[i], b.children[i])) return false;
    return true;
}
bool sameMembership(const ZfsOwnershipRead& a, const ZfsOwnershipRead& b) {
    if (a.pools.size() != b.pools.size()) return false;
    for (std::size_t i = 0; i < a.pools.size(); ++i) {
        const auto& x = a.pools[i];
        const auto& y = b.pools[i];
        if (std::tie(x.name, x.guid, x.complete) != std::tie(y.name, y.guid, y.complete) ||
            !sameVdev(x.root, y.root)) return false;
    }
    return true;
}
}
ZfsOwnershipRead NativeZfsOwnershipSource::read() {
    // Reject libzfs's testing filters rather than accepting a filtered namespace.
    if (environmentPresent("__ZFS_POOL_EXCLUDE") || environmentPresent("__ZFS_POOL_RESTRICT"))
        return {false, {}, "Libzfs pool filtering environment prevents complete ownership enumeration"};

    // Open the existing control node before libzfs_init. Its open file pins the
    // loaded Linux ZFS module throughout both captures, avoiding init's modprobe
    // fallback. Missing control access is unavailable evidence, never activation.
    ControlGuard guard{::open("/dev/zfs", O_RDONLY | O_CLOEXEC)};
    struct stat status {};
    if (guard.value < 0 || ::fstat(guard.value, &status) != 0 || !S_ISCHR(status.st_mode))
        return {false, {}, "Existing ZFS control device unavailable; libzfs was not initialized"};

    auto out = capture();
    const auto again = capture(); // Fresh library namespace, not cached handles.
    if (!out.complete || !again.complete || !sameMembership(out, again)) {
        out.complete = false;
        for (auto& pool : out.pools) pool.complete = false;
        out.detail = "ZFS membership changed, incomplete, or could not be revalidated through public libzfs";
    }
    // Public APIs expose membership snapshots, not the private namespace cookie.
    // Equality is a bounded recheck, not an atomicity or execution guarantee.
    return out;
}
} // namespace drivelab
#else
namespace drivelab {
ZfsOwnershipRead NativeZfsOwnershipSource::read() {
    return {false, {}, "Built without a usable public OpenZFS interface; live membership unavailable"};
}
} // namespace drivelab
#endif
