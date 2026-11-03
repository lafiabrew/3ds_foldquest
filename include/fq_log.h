#ifndef FQ_LOG_H
#define FQ_LOG_H
/* FoldQuest diagnostic export. Diagnostic JSONL ONLY (3ds/logs/3ds_foldquest.log) through the
 * vendored lafiabrew journal. It is NOT game-save storage: nothing here is ever read back to
 * restore a game, and RAM state is the only game state. */
#include "fq_core.h"
#include <stddef.h>

#define FQ_LOG_APP "3ds_foldquest"
#define FQ_LOG_RELATIVE_PATH "3ds/logs/3ds_foldquest.log"
#define FQ_LOG_LIMIT_BYTES (256u * 1024u)

typedef struct {
    const char* sd_root;   /* "sdmc:" natively, a directory on host */
    const char* build;     /* [A-Za-z0-9._-]{1,32} */
    const char* model;     /* "o3ds" / "n3ds" from a REAL query, or NULL when unknown */
    bool touch, apt_hooks; /* capabilities actually present in this build/platform */
} FqLogOptions;

typedef enum {
    FQ_LOG_NONE = 0,
    FQ_LOG_EXPORTED,
    FQ_LOG_RECOVERED, /* an interrupted earlier export completed first */
    FQ_LOG_FAILED,    /* nothing published; committed bytes untouched */
    FQ_LOG_PENDING,   /* intent/cleanup/archive close outstanding; retry */
    FQ_LOG_REFUSED    /* corrupt/unknown log state retained as evidence */
} FqLogState;

typedef struct {
    char active_path[1024];
    FqLogState state;
    unsigned recovered_transitions;
    bool committed, ownership_pending;
} FqLogResult;

/* 0 on a stable commit. event is [a-z_]{1,32}. Invalid input fails before any I/O. */
int fq_log_export(const FqLogOptions*, const FqGame*, const char* event, FqLogResult*);
/* Optionally one bounded recovery, then release archive ownership. Nonzero => still pending. */
int fq_log_shutdown(const FqLogOptions*, bool recover_pending, FqLogResult*);
const char* fq_log_status_text(const FqLogResult*);
/* Strict validator for committed bytes: whole ASCII records, our schema, first record <=199. */
int fq_log_validate(const unsigned char* data, size_t len);
#endif
