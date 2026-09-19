#include "test_support.h"
#include "platform/linux/linux_ownership_source.h"
#include <libzfs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdlib>

struct libzfs_handle_t { int pass; };
struct zpool_handle_t { std::string name; nvlist_t* config; int state; };

namespace {
nvlist_t config, root, disk, spare, cache;
int opens = 0, closes = 0, inits = 0, finis = 0, pool_closes = 0;
int fail_init = 0, fail_iter = 0, library_error = 0;
bool fail_open = false, fail_stat = false, not_character = false;
bool changed = false, disappeared = false, unavailable = false, missing_config = false;
bool empty = false, reverse_order = false;
libzfs_handle_t libraries[2]{{1}, {2}};

void filter(const char* name, const char* value) {
#ifdef _MSC_VER
    DL_CHECK(_putenv_s(name, value ? value : "") == 0);
#else
    DL_CHECK((value ? setenv(name, value, 1) : unsetenv(name)) == 0);
#endif
}
void reset() {
    filter("__ZFS_POOL_EXCLUDE", nullptr);
    filter("__ZFS_POOL_RESTRICT", nullptr);
    config = {}; root = {}; disk = {}; spare = {}; cache = {};
    opens = closes = inits = finis = pool_closes = 0;
    fail_init = fail_iter = library_error = 0;
    fail_open = fail_stat = not_character = changed = disappeared = unavailable = missing_config = false;
    empty = reverse_order = false;
    config.integers["pool_guid"] = 42;
    config.lists["vdev_tree"] = &root;
    root.strings["type"] = "root";
    root.integers["guid"] = 42;
    disk.strings = {{"type", "disk"}, {"path", "/dev/sda"}};
    disk.integers["guid"] = 1;
    spare.strings = {{"type", "disk"}, {"path", "/dev/sdb"}};
    spare.integers["guid"] = 2;
    cache.strings = {{"type", "disk"}, {"path", "/dev/sdc"}};
    cache.integers["guid"] = 3;
    root.arrays["children"] = {&disk};
    root.arrays["spares"] = {&spare};
    root.arrays["l2cache"] = {&cache};
}
}
extern "C" int open(const char* path, int flags, ...) {
    DL_CHECK(std::string(path) == "/dev/zfs" && flags == (O_RDONLY | O_CLOEXEC));
    ++opens;
    if (fail_open) { errno = EACCES; return -1; }
    return 42;
}
extern "C" int close(int fd) {
    DL_CHECK(fd == 42 && finis == inits - (fail_init ? 1 : 0));
    ++closes;
    return 0;
}
extern "C" int fstat(int fd, struct stat* out) {
    DL_CHECK(fd == 42);
    out->st_mode = not_character ? 0100000 : 0020000;
    return fail_stat ? -1 : 0;
}
extern "C" libzfs_handle_t* libzfs_init() {
    DL_CHECK(opens == 1 && closes == 0); // Existing module pinned before init.
    ++inits;
    if (inits == fail_init) return nullptr;
    DL_CHECK(inits <= 2);
    return &libraries[inits - 1];
}
extern "C" void libzfs_fini(libzfs_handle_t*) {
    DL_CHECK(closes == 0);
    ++finis;
}
extern "C" void libzfs_print_on_error(libzfs_handle_t*, boolean_t enabled) {
    DL_CHECK(enabled == B_FALSE);
}
extern "C" int libzfs_errno(libzfs_handle_t* handle) {
    return handle->pass == library_error ? 1 : EZFS_SUCCESS;
}
extern "C" int zpool_iter(libzfs_handle_t* handle, zpool_iter_f callback, void* data) {
    if (handle->pass == fail_iter) return -1;
    if (empty || (handle->pass == 2 && disappeared)) return 0;
    if (handle->pass == 2 && changed) disk.strings["path"] = "/dev/changed";
    const auto state = unavailable ? POOL_STATE_UNAVAIL : POOL_STATE_ACTIVE;
    zpool_handle_t first{"rpool", missing_config ? nullptr : &config, state};
    zpool_handle_t second{"other", &config, state};
    if (reverse_order && handle->pass == 2) {
        if (const int result = callback(&second, data)) return result;
        return callback(&first, data);
    }
    if (const int result = callback(&first, data)) return result;
    return reverse_order ? callback(&second, data) : 0;
}
extern "C" const char* zpool_get_name(zpool_handle_t* pool) { return pool->name.c_str(); }
extern "C" nvlist_t* zpool_get_config(zpool_handle_t* pool, nvlist_t** old) {
    DL_CHECK(old == nullptr);
    return pool->config;
}
extern "C" int zpool_get_state(zpool_handle_t* pool) { return pool->state; }
extern "C" void zpool_close(zpool_handle_t*) { ++pool_closes; }

extern "C" int nvlist_lookup_string(const nvlist_t* n, const char* key, const char** out) {
    if (!n->strings.contains(key)) return ENOENT;
    *out = n->strings.at(key).c_str();
    return 0;
}
extern "C" int nvlist_lookup_uint64(const nvlist_t* n, const char* key, std::uint64_t* out) {
    if (!n->integers.contains(key)) return ENOENT;
    *out = n->integers.at(key);
    return 0;
}
extern "C" int nvlist_lookup_nvlist(nvlist_t* n, const char* key, nvlist_t** out) {
    if (!n->lists.contains(key)) return ENOENT;
    *out = n->lists[key];
    return 0;
}
extern "C" int nvlist_lookup_nvlist_array(nvlist_t* n, const char* key, nvlist_t*** out, unsigned int* size) {
    if (!n->arrays.contains(key)) return ENOENT;
    *out = n->arrays[key].data();
    *size = static_cast<unsigned int>(n->arrays[key].size());
    return 0;
}
extern "C" boolean_t nvlist_exists(const nvlist_t* n, const char* key) {
    return (n->strings.contains(key) || n->integers.contains(key) ||
            n->lists.contains(key) || n->arrays.contains(key)) ? B_TRUE : B_FALSE;
}
int main() {
    return drivelab::test::run([] {
        drivelab::NativeZfsOwnershipSource source;
        reset();
        auto a = source.read();
        DL_CHECK(a.complete && a.pools.size() == 1 && a.pools[0].guid == "42");
        DL_CHECK(a.pools[0].root.children.size() == 3);
        DL_CHECK(inits == 2 && finis == 2 && pool_closes == 2 && closes == 1);
        reset(); reverse_order = true;
        DL_CHECK(source.read().complete && pool_closes == 4);
        reset(); empty = true;
        a = source.read();
        DL_CHECK(a.complete && a.pools.empty() && finis == 2 && closes == 1);
        reset(); changed = true;
        a = source.read();
        DL_CHECK(!a.complete && !a.pools[0].complete && closes == 1);
        reset(); disappeared = true;
        a = source.read();
        DL_CHECK(!a.complete && a.pools.size() == 1 && !a.pools[0].complete);
        reset(); fail_open = true;
        DL_CHECK(!source.read().complete && inits == 0 && closes == 0);
        reset(); fail_stat = true;
        DL_CHECK(!source.read().complete && inits == 0 && closes == 1);
        reset(); not_character = true;
        DL_CHECK(!source.read().complete && inits == 0 && closes == 1);
        for (int pass : {1, 2}) {
            reset(); fail_init = pass;
            DL_CHECK(!source.read().complete && closes == 1);
            reset(); fail_iter = pass;
            DL_CHECK(!source.read().complete && closes == 1);
            reset(); library_error = pass;
            DL_CHECK(!source.read().complete && closes == 1);
        }
        reset(); unavailable = true;
        a = source.read();
        DL_CHECK(!a.complete && !a.pools[0].complete);
        reset(); missing_config = true;
        DL_CHECK(!source.read().complete && pool_closes == 2);
        reset(); disk.strings.erase("type");
        a = source.read();
        DL_CHECK(!a.pools[0].root.children[0].complete);
        reset(); config.integers.clear();
        DL_CHECK(!source.read().complete && pool_closes == 2);
        for (const char* name : {"__ZFS_POOL_EXCLUDE", "__ZFS_POOL_RESTRICT"}) {
            reset(); filter(name, "rpool");
            DL_CHECK(!source.read().complete && inits == 0 && opens == 0);
        }
        reset();
    });
}
