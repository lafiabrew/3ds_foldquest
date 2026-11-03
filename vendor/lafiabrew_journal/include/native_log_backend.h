#ifndef NATIVE_LOG_BACKEND_H
#define NATIVE_LOG_BACKEND_H
/* Namespace backend ownership for the native diagnostic-log journal.
 * Native (__3DS__): src/native_fs_3ds.c owns one FSUSER SDMC archive handle and uses
 * FSUSER_RenameFile (fails on an existing destination; never deletes it).
 * HOST NATIVE MODEL: src/native_fs_host.c simulates native storage with
 * renameat2(RENAME_NOREPLACE). It is a model, not SD or power-loss evidence. */
#include "native_log_store.h"

typedef struct {
    unsigned opens, open_failures, closes, close_failures;
    bool held, close_pending;
} LinkLabNativeFsState;

/* 0: backend ready. -1: acquisition failed (nothing held). -2: a handle whose close
 * failed is still retained; its close was retried and failed again, nothing reopened. */
int linklab_native_fs_acquire(const NlsBackend** out);
/* 0: closed or nothing held. -1: close failed; the handle stays owned for retry. */
int linklab_native_fs_release(void);
/* Explicit shutdown: same ownership rule as release, safe to repeat. */
int linklab_native_fs_shutdown(void);
void linklab_native_fs_state(LinkLabNativeFsState*);
const char* linklab_native_fs_origin(void);
#endif
