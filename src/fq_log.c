#define _POSIX_C_SOURCE 200809L
#include "fq_log.h"
#include "native_log_backend.h"
#include "native_log_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int safe_text(const char* s, size_t max, int allow_dot_dash_upper) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n > max) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9');
        if (allow_dot_dash_upper) ok = ok || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

static int record(char* out, size_t cap, const char* build, const char* event, uint64_t elapsed,
                  const char* settings, const char* caps) {
    int n = snprintf(out, cap,
        "{\"schema\":1,\"app\":\"" FQ_LOG_APP "\",\"build\":\"%s\",\"event\":\"%s\",\"elapsed_us\":%llu,"
        "\"seed\":%u,\"settings\":%s,\"capabilities\":%s}\n",
        build, event, (unsigned long long)elapsed, FQ_LEVEL_ID, settings, caps);
    return n > 0 && (size_t)n < cap ? n : -1;
}

/* ---- strict validator: we are the only writer, so anything off-grammar is evidence, not data ---- */
typedef struct { const char* p; const char* end; } Cur;
static int lit(Cur* c, const char* s) {
    size_t n = strlen(s);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, s, n)) return 0;
    c->p += n; return 1;
}
static int word(Cur* c, int quoted_close, const char** b, const char** e, int allow_text_chars) {
    *b = c->p;
    while (c->p < c->end) {
        char ch = *c->p;
        int ok = (ch >= 'a' && ch <= 'z') || ch == '_' || (ch >= '0' && ch <= '9') ||
                 (allow_text_chars && ((ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-'));
        if (!ok) break;
        c->p++;
    }
    *e = c->p;
    return *e > *b && c->p < c->end && *c->p == '"' && quoted_close;
}
static int digits(Cur* c) {
    const char* b = c->p;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') c->p++;
    size_t n = (size_t)(c->p - b);
    return n >= 1 && n <= 20 && (n == 1 || *b != '0');
}
static int object(Cur* c) {
    if (!lit(c, "{")) return 0;
    if (lit(c, "}")) return 1;
    for (;;) {
        const char *b, *e;
        if (!lit(c, "\"") || !word(c, 1, &b, &e, 0) || !lit(c, "\":")) return 0;
        if (lit(c, "\"")) { if (!lit(c, "\"") && (!word(c, 1, &b, &e, 1) || !lit(c, "\""))) return 0; }
        else if (!(lit(c, "true") || lit(c, "false") || lit(c, "null") || digits(c))) return 0;
        if (lit(c, "}")) return 1;
        if (!lit(c, ",")) return 0;
    }
}
static int valid_record(const char* line, size_t len, int first) {
    Cur c = {line, line + len};
    const char *b, *e;
    if (len > 4096 || (first && len > 199)) return 0;
    if (!lit(&c, "{\"schema\":1,\"app\":\"" FQ_LOG_APP "\",\"build\":\"")) return 0;
    if (!word(&c, 1, &b, &e, 1) || !lit(&c, "\",\"event\":\"")) return 0;
    if (!word(&c, 1, &b, &e, 0)) return 0;
    if (first && !((size_t)(e - b) == 8 && !memcmp(b, "log_open", 8))) return 0;
    if (!lit(&c, "\",\"elapsed_us\":") || !digits(&c) || !lit(&c, ",\"seed\":") || !digits(&c)) return 0;
    if (!lit(&c, ",\"settings\":") || !object(&c) || !lit(&c, ",\"capabilities\":") || !object(&c)) return 0;
    return lit(&c, "}") && c.p == c.end;
}
int fq_log_validate(const unsigned char* data, size_t len) {
    size_t start = 0, records = 0;
    if (!data || len > FQ_LOG_LIMIT_BYTES) return 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] > 126 || (data[i] < 32 && data[i] != '\n')) return 0;
        if (data[i] != '\n') continue;
        if (!valid_record((const char*)data + start, i - start, records == 0)) return 0;
        records++; start = i + 1;
    }
    return start == len && records != 0;
}

static int mkdir_ok(const char* path) {
    struct stat st;
    if (!mkdir(path, 0775)) return 0;
    return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode) ? 0 : -1;
}
/* Device root "sdmc:" is a mount: stat "sdmc:/", never mkdir the device name. */
static int root_ready(const char* root) {
    size_t n = strlen(root);
    if (n && root[n - 1] == ':') {
        char path[64]; struct stat st;
        if (snprintf(path, sizeof(path), "%s/", root) >= (int)sizeof(path)) return -1;
        return !stat(path, &st) && S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir_ok(root);
}
static int paths(const FqLogOptions* o, FqLogResult* r) {
    if (!o || !o->sd_root || !*o->sd_root || strstr(o->sd_root, "..")) return -1;
    return snprintf(r->active_path, sizeof(r->active_path), "%s/" FQ_LOG_RELATIVE_PATH, o->sd_root) >=
                   (int)sizeof(r->active_path) ? -1 : 0;
}
static FqLogState state_of(NlsStatus s) {
    return s == NLS_REFUSED ? FQ_LOG_REFUSED : s == NLS_PENDING ? FQ_LOG_PENDING : FQ_LOG_FAILED;
}

int fq_log_export(const FqLogOptions* o, const FqGame* g, const char* event, FqLogResult* r) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r)); r->state = FQ_LOG_FAILED;
    if (!o || !g || !safe_text(o->build, 32, 1) || !safe_text(event, 32, 0) || paths(o, r)) return -1;
    if (o->model && strcmp(o->model, "o3ds") && strcmp(o->model, "n3ds")) return -1;
    char settings[640], caps[256], header[256], line[1100], dir[1100], parent[1100];
    snprintf(settings, sizeof(settings),
        "{\"level\":%u,\"moves\":%u,\"phase\":%u,\"solved\":%s,\"asleep\":%s,\"resumes\":%u,"
        "\"resumes_lid\":%u,\"resumes_home\":%u,\"practice_folds\":%u,\"resets\":%u,\"squeezes\":%u,"
        "\"blocked\":%u,\"rejected_asleep\":%u,\"spurious_resumes\":%u,\"redundant_suspends\":%u}",
        FQ_LEVEL_ID, g->moves, (unsigned)g->phase, g->solved ? "true" : "false", g->asleep ? "true" : "false",
        g->resumes, g->resumes_by_cause[FQ_CAUSE_LID_SLEEP], g->resumes_by_cause[FQ_CAUSE_HOME_SUSPEND],
        g->practice_folds, g->resets, g->squeezes, g->blocked, g->rejected_asleep, g->spurious_resumes,
        g->redundant_suspends);
    snprintf(caps, sizeof(caps),
        "{\"model\":%s%s%s,\"touch\":%s,\"apt_hooks\":%s,\"new3ds_enhancements\":false}",
        o->model ? "\"" : "", o->model ? o->model : "null", o->model ? "\"" : "",
        o->touch ? "true" : "false", o->apt_hooks ? "true" : "false");
    int hn = record(header, sizeof(header), o->build, "log_open", 0, "{}", "{\"model\":null}");
    int ln = record(line, sizeof(line), o->build, event, g->awake_us, settings, caps);
    if (hn < 0 || hn - 1 > 199 || ln < 0) return -1;
    if (snprintf(parent, sizeof(parent), "%s/3ds", o->sd_root) >= (int)sizeof(parent) ||
        snprintf(dir, sizeof(dir), "%s/3ds/logs", o->sd_root) >= (int)sizeof(dir)) return -1;
    if (root_ready(o->sd_root) || mkdir_ok(parent) || mkdir_ok(dir)) return -1;

    const NlsBackend* backend = NULL;
    int acquired = linklab_native_fs_acquire(&backend);
    if (acquired) {
        r->ownership_pending = acquired == -2;
        r->state = acquired == -2 ? FQ_LOG_PENDING : FQ_LOG_FAILED;
        return -1;
    }
    NativeLogStore store;
    uint8_t *old = NULL, *next = NULL;
    int rc = -1;
    if (nls_init(&store, r->active_path, backend)) goto release;
    NlsStatus status = nls_recover(&store); /* BEFORE any staging: a pending intent is never replaced */
    r->recovered_transitions = store.transitions;
    if (status < 0) { r->state = state_of(status); goto release; }
    old = malloc(FQ_LOG_LIMIT_BYTES); next = malloc(FQ_LOG_LIMIT_BYTES);
    if (!old || !next) goto release;
    size_t size = 0;
    int found = nls_read_active(&store, old, FQ_LOG_LIMIT_BYTES, &size);
    if (found < 0) { r->state = found == -2 ? FQ_LOG_REFUSED : FQ_LOG_FAILED; goto release; }
    if (found == 1) size = 0;
    else if (size && !fq_log_validate(old, size)) { r->state = FQ_LOG_REFUSED; goto release; }
    bool rotate = size + (size_t)ln > FQ_LOG_LIMIT_BYTES;
    size_t n = 0;
    if (size && !rotate) { memcpy(next, old, size); n = size; }
    if (!size || rotate) { memcpy(next + n, header, (size_t)hn); n += (size_t)hn; }
    memcpy(next + n, line, (size_t)ln); n += (size_t)ln;
    status = nls_commit(&store, next, n, rotate);
    if (status < 0) { r->state = state_of(status); goto release; }
    r->committed = true;
    rc = 0;
release:
    free(old); free(next);
    if (linklab_native_fs_release()) { r->ownership_pending = true; r->state = FQ_LOG_PENDING; rc = -1; }
    if (!rc) r->state = r->recovered_transitions ? FQ_LOG_RECOVERED : FQ_LOG_EXPORTED;
    return rc;
}

int fq_log_shutdown(const FqLogOptions* o, bool recover_pending, FqLogResult* r) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r)); r->state = FQ_LOG_EXPORTED;
    int rc = 0;
    if (recover_pending) {
        const NlsBackend* backend = NULL;
        NativeLogStore store;
        int acquired = paths(o, r) ? -1 : linklab_native_fs_acquire(&backend);
        if (acquired || nls_init(&store, r->active_path, backend)) {
            rc = -1; r->state = FQ_LOG_PENDING; r->ownership_pending = acquired == -2;
        } else {
            NlsStatus s = nls_recover(&store);
            r->recovered_transitions = store.transitions;
            if (s < 0) { rc = -1; r->state = state_of(s); }
            else if (s == NLS_RECOVERED) r->state = FQ_LOG_RECOVERED;
        }
    }
    if (linklab_native_fs_shutdown()) { rc = -1; r->ownership_pending = true; r->state = FQ_LOG_PENDING; }
    return rc;
}

const char* fq_log_status_text(const FqLogResult* r) {
    if (!r) return "EXPORT FAILED";
    switch (r->state) {
    case FQ_LOG_EXPORTED: return "EXPORTED: sdmc:/" FQ_LOG_RELATIVE_PATH;
    case FQ_LOG_RECOVERED: return "RECOVERED prior export; EXPORTED";
    case FQ_LOG_PENDING: return r->ownership_pending ? "RECOVERY PENDING: SD close failed. X retry"
                                                      : "RECOVERY PENDING: interrupted export kept. X retry";
    case FQ_LOG_REFUSED: return "EXPORT FAILED: unknown/corrupt log kept";
    case FQ_LOG_FAILED: case FQ_LOG_NONE: break;
    }
    return "EXPORT FAILED: SD/path/write. X retry";
}
