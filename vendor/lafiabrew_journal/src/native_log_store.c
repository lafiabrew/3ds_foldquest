#define _POSIX_C_SOURCE 200809L
/* Native diagnostic-log journal store. See include/native_log_store.h and
 * specs/log-contract.md. Single writer, fixed namespace, bounded recovery. */
#include "native_log_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __3DS__
/* FAT SD has no symlinks; libctru's devoptab implements stat, not a distinct lstat. */
#define NLS_LSTAT stat
#else
#define NLS_LSTAT lstat
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* Checkpoint registry. The first 18 are the normative logical stages; the
 * remaining ones name the staging writes and the abandon-staging cleanup. */
static const char* const stage_names[] = {
    "scan_old", "scan_new", "scan_previous", "verify_prefix", "intent_open",
    "intent_write", "intent_flush", "intent_close", "intent_verify", "backup_active",
    "backup_previous", "publish_active", "verify_active", "publish_previous",
    "verify_previous", "remove_old_backup", "remove_previous_backup", "remove_intent",
    "stage_open", "stage_write", "stage_flush", "stage_close", "remove_stage"};

const char* const* nls_stage_names(size_t* count) {
    if (count) *count = sizeof(stage_names) / sizeof(stage_names[0]);
    return stage_names;
}

#ifdef NLS_TEST_HOOKS
static NlsCheckpointHook hook;
void nls_set_checkpoint_hook(NlsCheckpointHook h) { hook = h; }
static int before(const char* stage) {
    if (hook && hook(stage, 0)) { errno = EIO; return -1; }
    return 0;
}
static void after(const char* stage) { if (hook) (void)hook(stage, 1); }
#else
static int before(const char* stage) { (void)stage; return 0; }
static void after(const char* stage) { (void)stage; }
#endif

const char* nls_status_name(NlsStatus status) {
    switch (status) {
    case NLS_STABLE: return "STABLE";
    case NLS_RECOVERED: return "RECOVERED";
    case NLS_FAILED: return "FAILED";
    case NLS_PENDING: return "PENDING";
    case NLS_REFUSED: return "REFUSED";
    }
    return "UNKNOWN";
}

uint32_t nls_crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (unsigned k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static const uint8_t magic[8] = {'N', 'L', 'O', 'G', 'T', 'X', '1', 0};

void nls_encode_intent(const NlsIntent* t, uint8_t o[NLS_INTENT_SIZE]) {
    memset(o, 0, NLS_INTENT_SIZE);
    memcpy(o, magic, sizeof(magic));
    put32(o + 8, 1); put32(o + 12, t->flags);
    put32(o + 16, t->old_len); put32(o + 20, t->old_crc);
    put32(o + 24, t->new_len); put32(o + 28, t->new_crc);
    put32(o + 32, t->prev_len); put32(o + 36, t->prev_crc);
    put32(o + 60, nls_crc32(o, 60));
}
/* Every flag combination/length is checked; anything not produced by nls_commit rejects. */
static int intent_semantics(const NlsIntent* t) {
    int old = !!(t->flags & NLS_FLAG_OLD), rot = !!(t->flags & NLS_FLAG_ROTATE);
    int prv = !!(t->flags & NLS_FLAG_PREVIOUS);
    if (t->flags & ~7u) return 0;
    if (!t->new_len || t->new_len > NLS_MAX_BYTES || t->old_len > NLS_MAX_BYTES || t->prev_len > NLS_MAX_BYTES) return 0;
    if (!old && (t->old_len || t->old_crc)) return 0;
    if (!prv && (t->prev_len || t->prev_crc)) return 0;
    if ((rot && !old) || (prv && !rot)) return 0;
    if (old && !rot && t->new_len <= t->old_len) return 0;
    return 1;
}
int nls_decode_intent(const uint8_t in[NLS_INTENT_SIZE], NlsIntent* t) {
    if (memcmp(in, magic, sizeof(magic)) || get32(in + 8) != 1) return -1;
    for (unsigned i = 40; i < 60; i++) if (in[i]) return -1;
    if (get32(in + 60) != nls_crc32(in, 60)) return -1;
    t->flags = get32(in + 12);
    t->old_len = get32(in + 16); t->old_crc = get32(in + 20);
    t->new_len = get32(in + 24); t->new_crc = get32(in + 28);
    t->prev_len = get32(in + 32); t->prev_crc = get32(in + 36);
    return intent_semantics(t) ? 0 : -1;
}

int nls_init(NativeLogStore* s, const char* active, const NlsBackend* b) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    size_t n = active ? strlen(active) : 0;
    if (n < 5 || strcmp(active + n - 4, ".log") || !b || !b->move_exclusive || !b->remove_file) return -1;
    s->backend = b;
    int base = (int)(n - 4);
    if (snprintf(s->active, sizeof(s->active), "%s", active) >= (int)sizeof(s->active) ||
        snprintf(s->stage, sizeof(s->stage), "%s.tmp", active) >= (int)sizeof(s->stage) ||
        snprintf(s->intent, sizeof(s->intent), "%s.txn", active) >= (int)sizeof(s->intent) ||
        snprintf(s->active_backup, sizeof(s->active_backup), "%s.bak", active) >= (int)sizeof(s->active_backup) ||
        snprintf(s->previous, sizeof(s->previous), "%.*s.previous", base, active) >= (int)sizeof(s->previous) ||
        snprintf(s->previous_backup, sizeof(s->previous_backup), "%.*s.previous.bak", base, active) >= (int)sizeof(s->previous_backup))
        return -1;
    return 0;
}

enum { K_ABSENT, K_FILE, K_UNSAFE, K_ERROR };
/* Classify WITHOUT opening: directories, symlinks, FIFOs and oversize files are unsafe. */
static int kind_of(const char* path, size_t limit) {
    struct stat st;
    if (NLS_LSTAT(path, &st)) return errno == ENOENT ? K_ABSENT : K_ERROR;
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long long)st.st_size > limit) return K_UNSAFE;
    return K_FILE;
}

typedef struct { int rc; size_t len; uint32_t crc, prefix_crc; } Probe; /* rc: 0 ok, 1 absent, -1 io, -2 unsafe */

/* Bounded whole-file read; never follows symlinks or opens FIFOs/directories. */
static int read_file(const char* stage, const char* path, uint8_t* buf, size_t cap, size_t* len) {
    int k = kind_of(path, cap);
    if (k == K_ABSENT) return 1;
    if (k == K_UNSAFE) return -2;
    if (k == K_ERROR || before(stage)) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    int rc = 0;
    size_t got = 0, size = 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long long)st.st_size > cap) rc = -2;
    else size = (size_t)st.st_size;
    while (!rc && got < size) {
        ssize_t r = read(fd, buf + got, size - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) rc = -1;
        else got += (size_t)r;
    }
    if (!rc) {
        char extra;
        if (read(fd, &extra, 1) != 0) rc = -1; /* grew or failed: not a stable generation */
    }
    if (close(fd)) rc = rc ? rc : -1; /* a descriptor is closed exactly once */
    if (!rc) { *len = got; after(stage); }
    return rc;
}
static void probe(const char* stage, const char* path, uint8_t* buf, size_t prefix, Probe* p) {
    memset(p, 0, sizeof(*p));
    p->rc = read_file(stage, path, buf, NLS_MAX_BYTES, &p->len);
    if (!p->rc) {
        p->crc = nls_crc32(buf, p->len);
        p->prefix_crc = prefix <= p->len ? nls_crc32(buf, prefix) : 0;
    }
}
static int matches(const Probe* p, uint32_t len, uint32_t crc) { return !p->rc && p->len == len && p->crc == crc; }

/* Exclusive creation + full write/flush/close. Returns 0 ok, -2 not created, -1 created
 * but incomplete. fclose() is called exactly once: even a failed fclose invalidates FILE. */
static int write_new(const char* const names[4], const char* path, const uint8_t* data, size_t len) {
    if (before(names[0])) return -2;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0664);
    if (fd < 0) return -2;
    after(names[0]);
    FILE* f = fdopen(fd, "wb");
    if (!f) { close(fd); return -1; }
    int ok = 1;
    if (before(names[1]) || fwrite(data, 1, len, f) != len) ok = 0;
    else after(names[1]);
    if (ok) {
        if (before(names[2]) || fflush(f)) ok = 0;
#ifndef __3DS__
        else if (fsync(fileno(f))) ok = 0; /* host model only; no device durability claim */
#endif
        else after(names[2]);
    }
    int injected = ok && before(names[3]);
    int closed = fclose(f);
    f = NULL;
    if (injected || closed) ok = 0;
    else if (ok) after(names[3]);
    return ok ? 0 : -1;
}
static int do_move(const NativeLogStore* s, const char* stage, const char* from, const char* to) {
    if (before(stage) || s->backend->move_exclusive(s->backend->ctx, from, to)) return -1;
    after(stage);
    return 0;
}
static int do_remove(const NativeLogStore* s, const char* stage, const char* path) {
    if (before(stage) || s->backend->remove_file(s->backend->ctx, path)) return -1;
    after(stage);
    return 0;
}

int nls_read_active(const NativeLogStore* s, uint8_t* buf, size_t cap, size_t* len) {
    return read_file("scan_old", s->active, buf, cap, len);
}

enum { ACT_REFUSE, ACT_IO, ACT_MOVE, ACT_REMOVE };
typedef struct { int type; const char *stage, *from, *to; } Action;
static Action refuse(void) { Action a = {ACT_REFUSE, NULL, NULL, NULL}; return a; }
static Action act(int type, const char* stage, const char* from, const char* to) {
    Action a = {type, stage, from, to}; return a;
}
/* Checks a probe against an expected generation. -1 io, 0 mismatch/unsafe, 1 match. */
static int expect(const Probe* p, uint32_t len, uint32_t crc) {
    if (p->rc == -1) return -1;
    return matches(p, len, crc);
}
#define EXPECT(p, len, crc) do { \
        int e_ = expect(&(p), (len), (crc)); \
        if (e_ <= 0) return e_ < 0 ? act(ACT_IO, NULL, NULL, NULL) : refuse(); \
    } while (0)

/* Recognized placements only; anything else is refused with every byte retained. */
static Action decide(const NativeLogStore* s, const NlsIntent* t, const int k[6], uint8_t* buf) {
    enum { A, S, I, B, P, Q };
    int old = !!(t->flags & NLS_FLAG_OLD), rot = !!(t->flags & NLS_FLAG_ROTATE);
    int prv = !!(t->flags & NLS_FLAG_PREVIOUS);
    Probe pa, ps, pb, pp, pq;
    if (k[S] == K_FILE) { /* staged generation not yet published */
        probe("scan_new", s->stage, buf, 0, &ps);
        EXPECT(ps, t->new_len, t->new_crc);
        if (k[A] == K_FILE) {
            if (!old || k[B] != K_ABSENT || k[Q] != K_ABSENT) return refuse();
            probe("scan_old", s->active, buf, 0, &pa);
            EXPECT(pa, t->old_len, t->old_crc);
            if (rot && prv) {
                if (k[P] != K_FILE) return refuse();
                probe("scan_previous", s->previous, buf, 0, &pp);
                EXPECT(pp, t->prev_len, t->prev_crc);
            } else if (rot && k[P] != K_ABSENT) return refuse();
            return act(ACT_MOVE, "backup_active", s->active, s->active_backup);
        }
        if (!old) {
            if (k[B] != K_ABSENT || k[Q] != K_ABSENT) return refuse();
            return act(ACT_MOVE, "publish_active", s->stage, s->active);
        }
        if (k[B] != K_FILE) return refuse(); /* old generation missing: never publish over it */
        probe("scan_old", s->active_backup, buf, 0, &pb);
        EXPECT(pb, t->old_len, t->old_crc);
        if (rot && prv) {
            if (k[P] == K_FILE && k[Q] == K_ABSENT) {
                probe("scan_previous", s->previous, buf, 0, &pp);
                EXPECT(pp, t->prev_len, t->prev_crc);
                return act(ACT_MOVE, "backup_previous", s->previous, s->previous_backup);
            }
            if (k[P] != K_ABSENT || k[Q] != K_FILE) return refuse();
            probe("scan_previous", s->previous_backup, buf, 0, &pq);
            EXPECT(pq, t->prev_len, t->prev_crc);
        } else if (k[Q] != K_ABSENT || (rot && k[P] != K_ABSENT)) return refuse();
        return act(ACT_MOVE, "publish_active", s->stage, s->active);
    }
    /* Published: active must be exactly the new generation. */
    if (k[A] != K_FILE) return refuse();
    probe("verify_active", s->active, buf, t->old_len, &pa);
    EXPECT(pa, t->new_len, t->new_crc);
    if (!old) {
        if (k[B] != K_ABSENT || k[Q] != K_ABSENT) return refuse();
        return act(ACT_REMOVE, "remove_intent", s->intent, NULL);
    }
    if (!rot) {
        if (k[Q] != K_ABSENT) return refuse();
        if (k[B] == K_ABSENT) return act(ACT_REMOVE, "remove_intent", s->intent, NULL);
        uint32_t prefix = pa.prefix_crc;
        probe("scan_old", s->active_backup, buf, 0, &pb);
        EXPECT(pb, t->old_len, t->old_crc);
        /* The complete old prefix must be retained in active before its backup goes. */
        if (before("verify_prefix")) return act(ACT_IO, NULL, NULL, NULL);
        if (prefix != t->old_crc) return refuse();
        after("verify_prefix");
        return act(ACT_REMOVE, "remove_old_backup", s->active_backup, NULL);
    }
    if (k[B] == K_FILE) {
        probe("scan_old", s->active_backup, buf, 0, &pb);
        EXPECT(pb, t->old_len, t->old_crc);
        if (k[P] != K_ABSENT) return refuse();
        if (prv) {
            if (k[Q] != K_FILE) return refuse();
            probe("scan_previous", s->previous_backup, buf, 0, &pq);
            EXPECT(pq, t->prev_len, t->prev_crc);
        } else if (k[Q] != K_ABSENT) return refuse();
        return act(ACT_MOVE, "publish_previous", s->active_backup, s->previous);
    }
    if (k[B] != K_ABSENT || k[P] != K_FILE) return refuse();
    probe("verify_previous", s->previous, buf, 0, &pp);
    EXPECT(pp, t->old_len, t->old_crc);
    if (k[Q] == K_ABSENT) return act(ACT_REMOVE, "remove_intent", s->intent, NULL);
    if (!prv) return refuse();
    probe("scan_previous", s->previous_backup, buf, 0, &pq);
    EXPECT(pq, t->prev_len, t->prev_crc);
    return act(ACT_REMOVE, "remove_previous_backup", s->previous_backup, NULL);
}

NlsStatus nls_recover(NativeLogStore* s) {
    if (!s || !s->backend) return NLS_FAILED;
    s->transitions = 0;
    uint8_t* buf = malloc(NLS_MAX_BYTES);
    if (!buf) return NLS_FAILED;
    NlsStatus result = NLS_PENDING;
    const char* names[6] = {s->active, s->stage, s->intent, s->active_backup, s->previous, s->previous_backup};
    for (unsigned step = 0;; step++) {
        int k[6], error = 0, unsafe = 0;
        for (unsigned i = 0; i < 6; i++) {
            k[i] = kind_of(names[i], i == 2 ? NLS_INTENT_SIZE : NLS_MAX_BYTES);
            error |= k[i] == K_ERROR; unsafe |= k[i] == K_UNSAFE;
        }
        if (error) { result = NLS_PENDING; break; }
        if (unsafe) { result = NLS_REFUSED; break; }
        if (k[2] == K_ABSENT) {
            /* Sidecars without an intent (e.g. a legacy orphan .tmp) are unknown evidence. */
            if (k[1] != K_ABSENT || k[3] != K_ABSENT || k[5] != K_ABSENT) result = NLS_REFUSED;
            else result = s->transitions ? NLS_RECOVERED : NLS_STABLE;
            break;
        }
        if (step >= NLS_MAX_TRANSITIONS) { result = NLS_PENDING; break; }
        uint8_t raw[NLS_INTENT_SIZE];
        size_t len = 0;
        NlsIntent t;
        int rc = read_file("intent_verify", s->intent, raw, sizeof(raw), &len);
        if (rc == -1) { result = NLS_PENDING; break; }
        if (rc || len != NLS_INTENT_SIZE || nls_decode_intent(raw, &t)) { result = NLS_REFUSED; break; }
        Action a = decide(s, &t, k, buf);
        if (a.type == ACT_REFUSE) { result = NLS_REFUSED; break; }
        if (a.type == ACT_IO) { result = NLS_PENDING; break; }
        if (a.type == ACT_MOVE ? do_move(s, a.stage, a.from, a.to) : do_remove(s, a.stage, a.from)) {
            result = NLS_PENDING; break;
        }
        s->transitions++;
    }
    free(buf);
    return result;
}

/* Undo staging that THIS call created before any namespace move. Intent first. */
static NlsStatus abandon(const NativeLogStore* s, int intent_created) {
    if (intent_created && do_remove(s, "remove_intent", s->intent)) return NLS_PENDING;
    if (do_remove(s, "remove_stage", s->stage)) return NLS_PENDING;
    return NLS_FAILED;
}

NlsStatus nls_commit(NativeLogStore* s, const uint8_t* data, size_t len, bool rotate) {
    static const char* const stage_ops[4] = {"stage_open", "stage_write", "stage_flush", "stage_close"};
    static const char* const intent_ops[4] = {"intent_open", "intent_write", "intent_flush", "intent_close"};
    if (!s || !s->backend || !data || !len || len > NLS_MAX_BYTES) return NLS_FAILED;
    const char* sidecars[4] = {s->stage, s->intent, s->active_backup, s->previous_backup};
    for (unsigned i = 0; i < 4; i++) {
        int k = kind_of(sidecars[i], NLS_MAX_BYTES);
        if (k == K_ERROR) return NLS_FAILED;
        if (k != K_ABSENT) return NLS_REFUSED; /* pending/unknown: recover first, never replace */
    }
    uint8_t* buf = malloc(NLS_MAX_BYTES);
    if (!buf) return NLS_FAILED;
    NlsStatus result = NLS_FAILED;
    NlsIntent t;
    memset(&t, 0, sizeof(t));
    Probe pa, pp, check;
    probe("scan_old", s->active, buf, 0, &pa);
    if (pa.rc == -2) { result = NLS_REFUSED; goto out; }
    if (pa.rc == -1 || (rotate && pa.rc)) goto out;
    if (!pa.rc) { t.flags |= NLS_FLAG_OLD; t.old_len = (uint32_t)pa.len; t.old_crc = pa.crc; }
    if (!rotate && !pa.rc) {
        if (before("verify_prefix")) goto out;
        if (len <= pa.len || memcmp(data, buf, pa.len)) { result = NLS_REFUSED; goto out; }
        after("verify_prefix");
    }
    if (rotate) {
        t.flags |= NLS_FLAG_ROTATE;
        probe("scan_previous", s->previous, buf, 0, &pp);
        if (pp.rc == -2) { result = NLS_REFUSED; goto out; }
        if (pp.rc == -1) goto out;
        if (!pp.rc) { t.flags |= NLS_FLAG_PREVIOUS; t.prev_len = (uint32_t)pp.len; t.prev_crc = pp.crc; }
    }
    t.new_len = (uint32_t)len;
    t.new_crc = nls_crc32(data, len);
    int rc = write_new(stage_ops, s->stage, data, len);
    if (rc == -2) goto out;
    if (rc) { result = abandon(s, 0); goto out; }
    probe("scan_new", s->stage, buf, 0, &check);
    if (!matches(&check, t.new_len, t.new_crc)) { result = abandon(s, 0); goto out; }
    uint8_t record[NLS_INTENT_SIZE], readback[NLS_INTENT_SIZE];
    size_t got = 0;
    nls_encode_intent(&t, record);
    rc = write_new(intent_ops, s->intent, record, sizeof(record));
    if (rc == -2) { result = abandon(s, 0); goto out; }
    if (rc || read_file("intent_verify", s->intent, readback, sizeof(readback), &got) ||
        got != sizeof(record) || memcmp(record, readback, sizeof(record))) {
        result = abandon(s, 1); goto out;
    }
    /* Intent is published: from here every failure leaves a recoverable PENDING state. */
    result = NLS_PENDING;
    if ((t.flags & NLS_FLAG_OLD) && do_move(s, "backup_active", s->active, s->active_backup)) goto out;
    if ((t.flags & NLS_FLAG_PREVIOUS) && do_move(s, "backup_previous", s->previous, s->previous_backup)) goto out;
    if (do_move(s, "publish_active", s->stage, s->active)) goto out;
    probe("verify_active", s->active, buf, 0, &check);
    if (!matches(&check, t.new_len, t.new_crc)) goto out;
    if (rotate) {
        if (do_move(s, "publish_previous", s->active_backup, s->previous)) goto out;
        probe("verify_previous", s->previous, buf, 0, &check);
        if (!matches(&check, t.old_len, t.old_crc)) goto out;
    } else if ((t.flags & NLS_FLAG_OLD) && do_remove(s, "remove_old_backup", s->active_backup)) goto out;
    if ((t.flags & NLS_FLAG_PREVIOUS) && do_remove(s, "remove_previous_backup", s->previous_backup)) goto out;
    if (do_remove(s, "remove_intent", s->intent)) goto out;
    result = NLS_STABLE;
out:
    free(buf);
    return result;
}
