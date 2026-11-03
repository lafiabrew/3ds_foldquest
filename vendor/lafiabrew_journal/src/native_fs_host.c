/* HOST NATIVE MODEL backend: simulated native storage for tests and the
 * `make native-storage-model` driver. Not SD-card, FSUSER or power-loss evidence. */
#define _GNU_SOURCE
#include "native_log_backend.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static LinkLabNativeFsState state;

/* Genuinely exclusive: the kernel refuses an existing destination atomically
 * (no racy lstat+rename, no overwrite, no delete-and-retry fallback). */
static int host_move(void* ctx, const char* from, const char* to) {
    (void)ctx;
    return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE) ? -1 : 0;
}
static int host_remove(void* ctx, const char* path) {
    (void)ctx;
    return unlink(path) ? -1 : 0;
}
static const NlsBackend backend = {
    NULL, "HOST NATIVE MODEL: simulated native storage (renameat2 RENAME_NOREPLACE); not SD/power-loss evidence",
    host_move, host_remove};

int linklab_native_fs_acquire(const NlsBackend** out) {
    state.opens++;
    state.held = true;
    *out = &backend;
    return 0;
}
int linklab_native_fs_release(void) {
    if (state.held) { state.held = false; state.closes++; }
    return 0;
}
int linklab_native_fs_shutdown(void) { return linklab_native_fs_release(); }
void linklab_native_fs_state(LinkLabNativeFsState* out) { *out = state; }
const char* linklab_native_fs_origin(void) { return backend.origin; }
