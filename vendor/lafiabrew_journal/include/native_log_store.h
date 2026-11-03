#ifndef NATIVE_LOG_STORE_H
#define NATIVE_LOG_STORE_H
/* Native diagnostic-log journal (R5, operator-approved native-log exception).
 * Scope: ONE single-writer app, ONE fixed diagnostic `.log` and its derived fixed
 * sidecars. Not a general save/config transaction API; no paths are ever read from
 * metadata. CRC32 values detect accidental corruption only; they are NOT
 * authentication. See specs/log-contract.md "Native journal". */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NLS_INTENT_SIZE 64u
#define NLS_MAX_BYTES (256u * 1024u)
#define NLS_MAX_TRANSITIONS 32u
#define NLS_PATH_MAX 1100
#define NLS_FLAG_OLD 1u      /* an old active generation existed */
#define NLS_FLAG_ROTATE 2u   /* old active becomes the retained .previous */
#define NLS_FLAG_PREVIOUS 4u /* an older .previous existed (rotation only) */

typedef enum {
    NLS_STABLE = 0,     /* no transaction outstanding (success) */
    NLS_RECOVERED = 1,  /* an interrupted transaction was rolled forward (success) */
    NLS_FAILED = -1,    /* failed before publishing intent; committed data untouched */
    NLS_PENDING = -2,   /* checked intent or cleanup outstanding; retry recovery */
    NLS_REFUSED = -3    /* unknown/corrupt/unsafe state; every byte retained */
} NlsStatus;

/* Namespace mutations go through the backend; reads/writes use POSIX file I/O.
 * move_exclusive MUST fail (errno EEXIST) when `to` exists and MUST NOT delete it. */
typedef struct {
    void* ctx;
    const char* origin;
    int (*move_exclusive)(void* ctx, const char* from, const char* to);
    int (*remove_file)(void* ctx, const char* path);
} NlsBackend;

typedef struct {
    char active[NLS_PATH_MAX], stage[NLS_PATH_MAX], intent[NLS_PATH_MAX];
    char active_backup[NLS_PATH_MAX], previous[NLS_PATH_MAX], previous_backup[NLS_PATH_MAX];
    const NlsBackend* backend;
    unsigned transitions; /* transitions performed by the last nls_recover call */
} NativeLogStore;

typedef struct {
    uint32_t flags, old_len, old_crc, new_len, new_crc, prev_len, prev_crc;
} NlsIntent;

/* active_path must end in ".log"; all sidecar names are derived, never read. */
int nls_init(NativeLogStore*, const char* active_path, const NlsBackend*);
/* Bounded (<= NLS_MAX_TRANSITIONS) idempotent recovery. Call BEFORE any staging. */
NlsStatus nls_recover(NativeLogStore*);
/* Requires a stable namespace (recover first). Never truncates/replaces a pending intent. */
NlsStatus nls_commit(NativeLogStore*, const uint8_t* data, size_t len, bool rotate);
/* Bounded regular-file read for the caller. 0 ok, 1 absent, -1 I/O error,
 * -2 unsafe (directory/symlink/FIFO/oversize): never opened. */
int nls_read_active(const NativeLogStore*, uint8_t* buf, size_t cap, size_t* len);

uint32_t nls_crc32(const uint8_t*, size_t);
void nls_encode_intent(const NlsIntent*, uint8_t out[NLS_INTENT_SIZE]);
int nls_decode_intent(const uint8_t in[NLS_INTENT_SIZE], NlsIntent*);
const char* const* nls_stage_names(size_t* count);
const char* nls_status_name(NlsStatus);

#ifdef NLS_TEST_HOOKS
/* Test-only checkpoint registry hook (never compiled into ARM builds).
 * Return nonzero from a "before" call to fail that operation with EIO. */
typedef int (*NlsCheckpointHook)(const char* stage, int after);
void nls_set_checkpoint_hook(NlsCheckpointHook);
#endif
#endif
