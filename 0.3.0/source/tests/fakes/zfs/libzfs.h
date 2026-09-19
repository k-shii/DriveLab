#pragma once
#include <libnvpair.h>
// Test doubles for the exported opaque userland handles, not a kernel ABI.
struct libzfs_handle_t;
struct zpool_handle_t;
enum { EZFS_SUCCESS = 0, POOL_STATE_ACTIVE = 10, POOL_STATE_UNAVAIL = 11 };
using zpool_iter_f = int (*)(zpool_handle_t*, void*);
extern "C" {
libzfs_handle_t* libzfs_init();
void libzfs_fini(libzfs_handle_t*);
void libzfs_print_on_error(libzfs_handle_t*, boolean_t);
int libzfs_errno(libzfs_handle_t*);
int zpool_iter(libzfs_handle_t*, zpool_iter_f, void*);
const char* zpool_get_name(zpool_handle_t*);
nvlist_t* zpool_get_config(zpool_handle_t*, nvlist_t**);
int zpool_get_state(zpool_handle_t*);
void zpool_close(zpool_handle_t*);
}
