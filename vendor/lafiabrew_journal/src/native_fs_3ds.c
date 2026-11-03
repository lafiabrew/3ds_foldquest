/* Native (libctru 2.7.0) namespace backend for the diagnostic-log journal.
 * libctru's libc rename()/archive_rename deletes an existing destination and retries,
 * so it is NOT used here. Moves call FSUSER_RenameFile directly on one owned SDMC
 * archive handle; an existing destination fails the move and is never deleted.
 * A handle whose FSUSER_CloseArchive failed stays owned: it is retried on the next
 * acquire/shutdown and no second archive is opened beside it. */
#include "native_log_backend.h"
#include <3ds.h>
#include <errno.h>
#include <string.h>

static FS_Archive archive;
static LinkLabNativeFsState state;

/* Only the SD device namespace; the store never derives paths from metadata. */
static int archive_path(const char* path, const char** out) {
    if (strncmp(path, "sdmc:/", 6)) return -1;
    *out = path + 5;
    return 0;
}
static int translate(Result rc) {
    if ((u32)rc == 0xC82044BEu || R_DESCRIPTION(rc) == RD_ALREADY_EXISTS) return EEXIST;
    if (R_DESCRIPTION(rc) == RD_NOT_FOUND) return ENOENT;
    return EIO;
}
static int fs_move(void* ctx, const char* from, const char* to) {
    const char *src, *dst;
    (void)ctx;
    if (!state.held || state.close_pending) { errno = EBADF; return -1; }
    if (archive_path(from, &src) || archive_path(to, &dst)) { errno = EINVAL; return -1; }
    Result rc = FSUSER_RenameFile(archive, fsMakePath(PATH_ASCII, src), archive, fsMakePath(PATH_ASCII, dst));
    if (R_FAILED(rc)) { errno = translate(rc); return -1; } /* no delete-and-retry */
    return 0;
}
static int fs_remove(void* ctx, const char* path) {
    const char* p;
    (void)ctx;
    if (!state.held || state.close_pending) { errno = EBADF; return -1; }
    if (archive_path(path, &p)) { errno = EINVAL; return -1; }
    Result rc = FSUSER_DeleteFile(archive, fsMakePath(PATH_ASCII, p));
    if (R_FAILED(rc)) { errno = translate(rc); return -1; }
    return 0;
}
static const NlsBackend backend = {
    NULL, "NATIVE libctru FSUSER SDMC archive (RenameFile no-overwrite)", fs_move, fs_remove};

int linklab_native_fs_release(void) {
    if (!state.held) return 0;
    Result rc = FSUSER_CloseArchive(archive);
    if (R_FAILED(rc)) {
        state.close_failures++;
        state.close_pending = true; /* keep the handle; do not drop or replace it */
        return -1;
    }
    state.closes++;
    state.held = false;
    state.close_pending = false;
    archive = 0;
    return 0;
}
int linklab_native_fs_acquire(const NlsBackend** out) {
    *out = NULL;
    if (state.held && state.close_pending && linklab_native_fs_release()) return -2;
    if (!state.held) {
        FS_Archive opened = 0;
        Result rc = FSUSER_OpenArchive(&opened, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
        if (R_FAILED(rc)) { state.open_failures++; return -1; }
        archive = opened;
        state.held = true;
        state.opens++;
    }
    *out = &backend;
    return 0;
}
int linklab_native_fs_shutdown(void) { return linklab_native_fs_release(); }
void linklab_native_fs_state(LinkLabNativeFsState* out) { *out = state; }
const char* linklab_native_fs_origin(void) { return backend.origin; }
