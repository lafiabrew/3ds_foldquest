/* FoldQuest host tests (ASan/UBSan). Each test drives PRODUCTION code (core, app, log store) and
 * asserts behavior/errors/cleanup; no test compares a fixture with itself. Host runs are a
 * simulation of lifecycle events only: they say nothing about 3DS hardware. */
#define _GNU_SOURCE
#include "fq_app.h"
#include "native_log_backend.h"
#include "native_log_store.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern const char* fq_host_drawn_text(void);
extern unsigned fq_host_drawn_rects(void);

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

static char root[256];
static void new_root(void) {
    snprintf(root, sizeof(root), "build/fq-test-XXXXXX");
    CHECK(mkdtemp(root));
}
static void path_of(char* out, size_t cap, const char* suffix) { snprintf(out, cap, "%s/3ds/logs/3ds_foldquest.log%s", root, suffix); }
static size_t slurp(const char* path, unsigned char* buf, size_t cap) {
    FILE* f = fopen(path, "rb");
    if (!f) return (size_t)-1;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static FqLogOptions opts(void) {
    FqLogOptions o = {root, "0.1.0", "o3ds", true, true};
    return o;
}
static void walk(FqGame* g, const char* keys) { /* u d l r */
    for (; *keys; keys++) {
        FqDir d = *keys == 'u' ? FQ_UP : *keys == 'd' ? FQ_DOWN : *keys == 'l' ? FQ_LEFT : FQ_RIGHT;
        fq_move(g, d);
    }
}
static void real_fold(FqGame* g) { CHECK(fq_suspend(g, FQ_CAUSE_LID_SLEEP) == FQ_OK); CHECK(fq_resume(g) == FQ_OK); }

/* Why: the magic moment. The room is unsolvable without two REAL resumes (gate a, then gate b). */
static void test_solve_requires_two_real_folds(void) {
    FqGame g; fq_init(&g);
    walk(&g, "rr");                                  /* up to the closed gate 'a' */
    CHECK(fq_move(&g, FQ_RIGHT) == FQ_BLOCKED && g.x == 3);
    CHECK(g.phase == 0 && g.resumes == 0);
    real_fold(&g);                                   /* fold 1: 'a' opens */
    CHECK(g.phase == 1 && g.resumes == 1);
    walk(&g, "rr");                                  /* through 'a', standing between gates */
    CHECK(g.x == 5);
    CHECK(fq_move(&g, FQ_RIGHT) == FQ_BLOCKED);      /* 'b' is closed in this fold */
    real_fold(&g);                                   /* fold 2: 'b' opens */
    CHECK(g.phase == 0 && g.resumes == 2 && g.resumes_by_cause[FQ_CAUSE_LID_SLEEP] == 2);
    walk(&g, "rrrr");
    CHECK(g.solved && g.x == 9 && g.moves == 8);
    CHECK(fq_move(&g, FQ_LEFT) == FQ_SOLVED && g.x == 9); /* solved room ignores further moves */
}

/* Why: "no hidden work while lid closed": asleep input and ticks change nothing, time stops. */
static void test_asleep_simulates_nothing(void) {
    FqGame g; fq_init(&g);
    CHECK(fq_tick(&g, 1000) == FQ_OK && g.awake_us == 1000);
    CHECK(fq_suspend(&g, FQ_CAUSE_HOME_SUSPEND) == FQ_OK);
    FqGame before = g;
    CHECK(fq_tick(&g, 5000000) == FQ_ASLEEP);
    CHECK(fq_move(&g, FQ_RIGHT) == FQ_ASLEEP);
    CHECK(fq_reset(&g) == FQ_ASLEEP);
    CHECK(fq_practice_fold(&g) == FQ_ASLEEP);
    CHECK(g.awake_us == before.awake_us && g.x == before.x && g.moves == before.moves && g.phase == before.phase);
    CHECK(g.rejected_asleep == 3);
    CHECK(fq_suspend(&g, FQ_CAUSE_LID_SLEEP) == FQ_ASLEEP && g.redundant_suspends == 1 && g.cause == FQ_CAUSE_HOME_SUSPEND);
    CHECK(fq_resume(&g) == FQ_OK && g.resumes_by_cause[FQ_CAUSE_HOME_SUSPEND] == 1);
}

/* Why: the resume transition happens ONCE per real resume; a stray resume must not flip the room. */
static void test_resume_counted_once_and_spurious_ignored(void) {
    FqGame g; fq_init(&g);
    CHECK(fq_resume(&g) == FQ_NOT_ASLEEP);
    CHECK(g.phase == 0 && g.resumes == 0 && g.spurious_resumes == 1);
    real_fold(&g);
    CHECK(fq_resume(&g) == FQ_NOT_ASLEEP);
    CHECK(g.phase == 1 && g.resumes == 1 && g.spurious_resumes == 2);
}

/* Why: a gate closing on the player must reset them rather than leave them inside a wall. */
static void test_squeeze_and_reset_and_practice(void) {
    FqGame g; fq_init(&g);
    walk(&g, "rr"); real_fold(&g); walk(&g, "r");    /* x=4: standing inside gate a */
    CHECK(g.x == 4 && fq_is_open(&g, 4, 1));
    real_fold(&g);                                   /* 'a' closes on us */
    CHECK(g.x == 1 && g.y == 1 && g.squeezes == 1 && g.phase == 0);
    CHECK(fq_practice_fold(&g) == FQ_OK && g.phase == 1 && g.practice_folds == 1 && g.resumes == 2);
    walk(&g, "d");
    CHECK(fq_reset(&g) == FQ_OK);
    CHECK(g.x == 1 && g.y == 1 && g.phase == 0 && g.moves == 0 && !g.solved && g.resets == 1 && g.resumes == 2);
}

/* Why: invalid inputs fail visibly (error codes) and never move or corrupt the game. */
static void test_invalid_inputs(void) {
    FqGame g; fq_init(&g);
    CHECK(fq_move(NULL, FQ_UP) == FQ_BAD_ARG && fq_move(&g, (FqDir)99) == FQ_BAD_ARG && fq_move(&g, (FqDir)-1) == FQ_BAD_ARG);
    CHECK(fq_move_to(&g, -1, 1) == FQ_BAD_ARG && fq_move_to(&g, 11, 1) == FQ_BAD_ARG && fq_move_to(&g, 1, 5) == FQ_BAD_ARG);
    CHECK(fq_move_to(&g, 3, 1) == FQ_BAD_ARG && fq_move_to(&g, 2, 2) == FQ_BAD_ARG && fq_move_to(&g, 1, 1) == FQ_BAD_ARG);
    CHECK(fq_suspend(&g, (FqCause)7) == FQ_BAD_ARG && fq_suspend(NULL, FQ_CAUSE_LID_SLEEP) == FQ_BAD_ARG);
    CHECK(fq_resume(NULL) == FQ_BAD_ARG && fq_tick(NULL, 1) == FQ_BAD_ARG && fq_reset(NULL) == FQ_BAD_ARG);
    CHECK(fq_move_to(&g, 1, 2) == FQ_OK && g.y == 2);
    CHECK(fq_move(&g, FQ_LEFT) == FQ_BLOCKED && g.blocked == 1 && g.moves == 1);
    CHECK(fq_tile(0, -1, 0) == ' ' && !fq_is_open(NULL, 1, 1));
}

/* Why: the lifecycle event queue is bounded; overflow drops the newest and is counted. */
static void test_event_queue_bounds(void) {
    FqEventQueue q = {0};
    FqLifecycleEvent e;
    CHECK(!fq_events_pop(&q, &e) && !fq_events_push(&q, FQ_EV_NONE) && !fq_events_push(&q, (FqLifecycleEvent)9));
    for (unsigned i = 0; i < FQ_EVENT_CAP; i++) CHECK(fq_events_push(&q, i % 2 ? FQ_EV_RESUME : FQ_EV_SUSPEND_LID));
    CHECK(!fq_events_push(&q, FQ_EV_RESUME) && q.dropped == 1);
    CHECK(fq_events_pop(&q, &e) && e == FQ_EV_SUSPEND_LID);
    CHECK(fq_events_push(&q, FQ_EV_SUSPEND_HOME));   /* wraps around */
    unsigned n = 0;
    while (fq_events_pop(&q, &e)) n++;
    CHECK(n == FQ_EVENT_CAP);
}

static FqInput press(uint32_t bits) { FqInput i = {bits, false, 0, 0}; return i; }
static FqInput tap(int x, int y) { FqInput i = {0, true, x, y}; return i; }
static void touch_cell(FqApp* a, int cx, int cy) {
    FqInput i = tap(FQ_GRID_X + cx * FQ_CELL + 5, FQ_GRID_Y + cy * FQ_CELL + 5);
    fq_app_frame(a, &i, 16667);
}

/* Why: the full user session through the app layer: buttons, touch, lifecycle events, draw, export. */
static void test_app_session_end_to_end(void) {
    new_root();
    FqLogOptions o = opts();
    FqApp a; fq_app_init(&a, &o);
    FqInput in = press(FQ_IN_RIGHT);
    fq_app_frame(&a, &in, 16667); in = press(FQ_IN_RIGHT); fq_app_frame(&a, &in, 16667);
    touch_cell(&a, 3, 1);                            /* touch adjacent tile */
    CHECK(a.game.x == 3);
    touch_cell(&a, 8, 3);                            /* far tile: refused */
    CHECK(a.game.x == 3 && strstr(a.status, "next to you"));
    in = press(FQ_IN_RIGHT); fq_app_frame(&a, &in, 16667);
    CHECK(strstr(a.status, "Blocked"));
    fq_app_lifecycle(&a, FQ_EV_SUSPEND_LID);
    in = press(FQ_IN_RIGHT); fq_app_frame(&a, &in, 99999999);
    CHECK(a.game.x == 3 && a.game.awake_us == 16667u * 5);
    CHECK(strstr(a.status, "Asleep"));
    fq_app_lifecycle(&a, FQ_EV_RESUME);
    CHECK(a.game.phase == 1 && strstr(a.status, "FOLDED"));
    fq_app_lifecycle(&a, FQ_EV_RESUME);              /* spurious */
    CHECK(a.game.phase == 1 && a.game.spurious_resumes == 1);
    for (int i = 0; i < 2; i++) { in = press(FQ_IN_RIGHT); fq_app_frame(&a, &in, 16667); }
    in = press(FQ_IN_PRACTICE); fq_app_frame(&a, &in, 16667);   /* practice fold: phase 0 */
    CHECK(a.game.phase == 0 && a.game.resumes == 1 && a.game.practice_folds == 1);
    for (int i = 0; i < 4; i++) { in = press(FQ_IN_RIGHT); fq_app_frame(&a, &in, 16667); }
    CHECK(a.game.solved && strstr(a.status, "SOLVED"));
    fq_app_draw(&a);
    CHECK(strstr(fq_host_drawn_text(), "FOLDQUEST") && strstr(fq_host_drawn_text(), "SOLVED") && fq_host_drawn_rects() > FQ_W * FQ_H);
    /* export via touch button */
    FqInput t = tap(250, FQ_BTN_Y + 10); fq_app_frame(&a, &t, 0);
    CHECK(a.last_export.state == FQ_LOG_EXPORTED && strstr(a.status, "EXPORTED"));
    unsigned char buf[4096]; char p[512]; path_of(p, sizeof(p), "");
    size_t n = slurp(p, buf, sizeof(buf));
    CHECK(n != (size_t)-1 && fq_log_validate(buf, n));
    buf[n] = 0;
    CHECK(strstr((char*)buf, "\"resumes\":1") && strstr((char*)buf, "\"practice_folds\":1") && strstr((char*)buf, "\"solved\":true"));
    in = press(FQ_IN_RESET); fq_app_frame(&a, &in, 0);
    CHECK(!a.game.solved && a.game.resumes == 1);
    in = press(FQ_IN_QUIT); fq_app_frame(&a, &in, 0);
    CHECK(a.quit && fq_app_shutdown(&a) == 0);
    FqInput none = {0}; FqApp* nul = NULL; fq_app_frame(nul, &none, 0); fq_app_frame(&a, NULL, 0);
}

/* Why: log schema/size contract plus immediate reuse of the REAL journal; first record <=199 ASCII. */
static void test_log_schema_and_append(void) {
    new_root();
    FqLogOptions o = opts(); FqGame g; fq_init(&g);
    FqLogResult r;
    g.awake_us = 123456;
    CHECK(fq_log_export(&o, &g, "export", &r) == 0 && r.state == FQ_LOG_EXPORTED && r.committed);
    CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    unsigned char buf[8192]; char p[512]; path_of(p, sizeof(p), "");
    size_t n = slurp(p, buf, sizeof(buf));
    CHECK(n > 0 && n < sizeof(buf) && fq_log_validate(buf, n));
    size_t first = (size_t)(strchr((char*)buf, '\n') - (char*)buf);
    CHECK(first <= 199);
    buf[n] = 0;
    CHECK(strstr((char*)buf, "\"event\":\"log_open\""));
    CHECK(strstr((char*)buf, "\"elapsed_us\":123456,\"seed\":1,"));
    CHECK(strstr((char*)buf, "\"model\":\"o3ds\""));
    unsigned lines = 0;
    for (char* c = (char*)buf; *c; c++) lines += *c == '\n';
    CHECK(lines == 3);                               /* log_open + 2 exports, not a re-opened log each time */
    /* unknown model is null, never invented */
    o.model = NULL; CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    n = slurp(p, buf, sizeof(buf)); buf[n] = 0;
    CHECK(strstr((char*)buf, "\"capabilities\":{\"model\":null,\"touch\":true"));
    CHECK(fq_log_shutdown(&o, false, &r) == 0);
}

/* Why: invalid parameters must fail BEFORE any storage change; JSON injection impossible. */
static void test_log_rejects_bad_parameters(void) {
    new_root();
    FqLogOptions o = opts(); FqGame g; fq_init(&g); FqLogResult r;
    char p[512]; path_of(p, sizeof(p), "");
    const char* bad_events[] = {"", "Export", "a\"b", "x\n", "has space", "this_event_name_is_far_too_long_for_us", NULL};
    for (unsigned i = 0; i < 6; i++) CHECK(fq_log_export(&o, &g, bad_events[i], &r) != 0 && r.state == FQ_LOG_FAILED);
    CHECK(fq_log_export(&o, &g, NULL, &r) != 0);
    o.build = "v\"1"; CHECK(fq_log_export(&o, &g, "export", &r) != 0);
    o.build = "0.1.0"; o.model = "new3ds"; CHECK(fq_log_export(&o, &g, "export", &r) != 0);
    o.model = "o3ds"; o.sd_root = "../escape"; CHECK(fq_log_export(&o, &g, "export", &r) != 0);
    o.sd_root = NULL; CHECK(fq_log_export(&o, &g, "export", &r) != 0);
    o.sd_root = root; CHECK(fq_log_export(&o, NULL, "export", &r) != 0 && fq_log_export(&o, &g, "export", NULL) != 0);
    CHECK(access(p, F_OK) != 0);                     /* nothing was created */
    CHECK(fq_log_export(&o, &g, "export", &r) == 0); /* valid retry works */
}

/* Why: unwritable/corrupt state is visible, retains every byte, and a valid retry succeeds. */
static void test_log_corruption_and_unwritable_retained(void) {
    new_root();
    FqLogOptions o = opts(); FqGame g; fq_init(&g); FqLogResult r;
    char p[512]; path_of(p, sizeof(p), "");
    CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    unsigned char good[4096], now[4096];
    size_t n = slurp(p, good, sizeof(good));
    /* torn tail (no newline) */
    FILE* f = fopen(p, "ab"); fputs("{\"schema\":1,\"app\":\"3ds_fo", f); fclose(f);
    size_t torn = slurp(p, good, sizeof(good));
    CHECK(torn > n);
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_REFUSED);
    CHECK(strstr(fq_log_status_text(&r), "kept"));
    CHECK(slurp(p, now, sizeof(now)) == torn && !memcmp(good, now, torn));   /* never silently trimmed */
    /* foreign app record */
    f = fopen(p, "wb"); fputs("{\"schema\":1,\"app\":\"3ds_linklab\",\"build\":\"0\",\"event\":\"log_open\",\"elapsed_us\":0,\"seed\":null,\"settings\":{},\"capabilities\":{\"model\":null}}\n", f); fclose(f);
    size_t foreign = slurp(p, good, sizeof(good));
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_REFUSED);
    CHECK(slurp(p, now, sizeof(now)) == foreign && !memcmp(good, now, foreign));
    /* NUL byte and non-ASCII byte */
    f = fopen(p, "wb"); fwrite("{\"a\":1}\n\0", 1, 9, f); fclose(f);
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_REFUSED);
    /* active is a directory: unsafe, refused, directory untouched */
    CHECK(unlink(p) == 0 && mkdir(p, 0775) == 0);
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state != FQ_LOG_EXPORTED);
    CHECK(rmdir(p) == 0);
    /* leftover staging without an intent is refused (could be foreign evidence) */
    char tmp[560]; snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    f = fopen(tmp, "wb"); fputs("orphan", f); fclose(f);
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_REFUSED);
    unsigned char orphan[16]; CHECK(slurp(tmp, orphan, sizeof(orphan)) == 6);
    CHECK(unlink(tmp) == 0);
    /* sd_root occupied by a file: visible failure; then valid retry on a good root */
    char file_root[300]; snprintf(file_root, sizeof(file_root), "%s/not_a_dir", root);
    f = fopen(file_root, "wb"); fputs("x", f); fclose(f);
    FqLogOptions bad = o; bad.sd_root = file_root;
    CHECK(fq_log_export(&bad, &g, "export", &r) != 0 && r.state == FQ_LOG_FAILED);
    CHECK(strstr(fq_log_status_text(&r), "FAILED"));
    CHECK(fq_log_export(&o, &g, "export", &r) == 0 && r.state == FQ_LOG_EXPORTED);
    n = slurp(p, good, sizeof(good));
    CHECK(fq_log_validate(good, n));
}

/* Why: validator unit boundaries (first-record 199-byte cap, grammar) on committed bytes. */
static void test_validator_boundaries(void) {
    const char* ok = "{\"schema\":1,\"app\":\"3ds_foldquest\",\"build\":\"0.1.0\",\"event\":\"log_open\",\"elapsed_us\":0,\"seed\":1,\"settings\":{},\"capabilities\":{\"model\":null}}\n";
    CHECK(fq_log_validate((const unsigned char*)ok, strlen(ok)));
    CHECK(!fq_log_validate(NULL, 4) && !fq_log_validate((const unsigned char*)"", 0));
    CHECK(!fq_log_validate((const unsigned char*)ok, strlen(ok) - 1));        /* torn */
    char bad[512];
    const char* variants[][2] = {{"\"schema\":1", "\"schema\":2"}, {"log_open", "export"}, {"\"elapsed_us\":0", "\"elapsed_us\":01"},
                                 {"\"seed\":1", "\"seed\":null"}, {"{}", "[]"}, {"3ds_foldquest", "3ds_foldquesT"}, {"\"build\":\"0.1.0\"", "\"build\":\"\""}};
    for (unsigned i = 0; i < 7; i++) {
        const char* at = strstr(ok, variants[i][0]);
        CHECK(at);
        snprintf(bad, sizeof(bad), "%.*s%s%s", (int)(at - ok), ok, variants[i][1], at + strlen(variants[i][0]));
        CHECK(!fq_log_validate((const unsigned char*)bad, strlen(bad)));
    }
    /* first record over 199 bytes is refused */
    char longfirst[512];
    snprintf(longfirst, sizeof(longfirst), "{\"schema\":1,\"app\":\"3ds_foldquest\",\"build\":\"0.1.0\",\"event\":\"log_open\",\"elapsed_us\":0,\"seed\":1,\"settings\":{\"%0100d\":1},\"capabilities\":{\"model\":null}}\n", 0);
    CHECK(strlen(longfirst) > 200 && !fq_log_validate((const unsigned char*)longfirst, strlen(longfirst)));
    /* a non-ASCII byte is refused */
    snprintf(bad, sizeof(bad), "%s", ok); bad[10] = (char)0xc3;
    CHECK(!fq_log_validate((const unsigned char*)bad, strlen(bad)));
}

/* Why: rotation near 256 KiB: old log moves whole to .previous; new active starts with a <=199B log_open. */
static void test_log_rotation_at_limit(void) {
    new_root();
    FqLogOptions o = opts(); FqGame g; fq_init(&g); FqLogResult r;
    char p[512], prev[560]; path_of(p, sizeof(p), "");
    CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    unsigned char one[4096]; size_t n = slurp(p, one, sizeof(one));
    const char* second = strchr((char*)one, '\n') + 1;
    size_t line = n - (size_t)(second - (char*)one);
    unsigned char* big = malloc(FQ_LOG_LIMIT_BYTES);
    CHECK(big);
    size_t used = 0;
    memcpy(big, one, (size_t)(second - (char*)one)); used = (size_t)(second - (char*)one);
    while (used + line + line <= FQ_LOG_LIMIT_BYTES) { memcpy(big + used, second, line); used += line; }
    CHECK(used + line > FQ_LOG_LIMIT_BYTES - line);
    FILE* f = fopen(p, "wb"); CHECK(fwrite(big, 1, used, f) == used); fclose(f);
    CHECK(fq_log_validate(big, used));
    /* pad with one more record so the NEXT export cannot fit */
    while (used + line <= FQ_LOG_LIMIT_BYTES - 0) { memcpy(big + used, second, line); used += line; }
    f = fopen(p, "wb"); CHECK(fwrite(big, 1, used, f) == used); fclose(f);
    CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    snprintf(prev, sizeof(prev), "%s/3ds/logs/3ds_foldquest.previous", root);
    unsigned char* got = malloc(FQ_LOG_LIMIT_BYTES);
    CHECK(got && slurp(prev, got, FQ_LOG_LIMIT_BYTES) == used && !memcmp(got, big, used));
    n = slurp(p, one, sizeof(one));
    CHECK(n != (size_t)-1 && fq_log_validate(one, n));
    CHECK(strchr((char*)one, '\n') - (char*)one <= 199 && strstr((char*)one, "\"event\":\"log_open\""));
    unsigned lines = 0; for (size_t i = 0; i < n; i++) lines += one[i] == '\n';
    CHECK(lines == 2);
    /* an oversize active file (beyond the cap) is unsafe and refused with bytes retained */
    f = fopen(p, "wb"); CHECK(fwrite(big, 1, used, f) == used); fputs("{\"x\":1}\n", f); fclose(f);
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_REFUSED);
    free(big); free(got);
}

static const char* hook_target;
static int hook(const char* stage, int after) { return !after && hook_target && !strcmp(stage, hook_target); }

/* Why: an interruption AFTER the intent is published leaves PENDING, never half-written active
 * bytes; the next export rolls it forward (RECOVERED) and the log stays valid with both records. */
static void test_log_interrupted_commit_recovers(void) {
    new_root();
    FqLogOptions o = opts(); FqGame g; fq_init(&g); FqLogResult r;
    char p[512]; path_of(p, sizeof(p), "");
    CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    unsigned char before[4096], after_[4096];
    size_t n0 = slurp(p, before, sizeof(before));
    nls_set_checkpoint_hook(hook);
    hook_target = "publish_active";
    g.moves = 3;
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_PENDING);
    CHECK(strstr(fq_log_status_text(&r), "PENDING"));
    hook_target = NULL;
    FqLogResult s;
    CHECK(fq_log_shutdown(&o, true, &s) == 0 && s.state == FQ_LOG_RECOVERED);   /* bounded recovery on quit */
    size_t n1 = slurp(p, after_, sizeof(after_));
    CHECK(n1 > n0 && fq_log_validate(after_, n1) && !memcmp(before, after_, n0));
    after_[n1] = 0;
    CHECK(strstr((char*)after_, "\"moves\":3"));
    /* and a pending state followed by a normal export: RECOVERED then appended */
    hook_target = "publish_active"; g.moves = 4;
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_PENDING);
    hook_target = NULL; g.moves = 5;
    CHECK(fq_log_export(&o, &g, "export", &r) == 0 && r.state == FQ_LOG_RECOVERED);
    size_t n2 = slurp(p, after_, sizeof(after_)); after_[n2] = 0;
    CHECK(fq_log_validate(after_, n2) && strstr((char*)after_, "\"moves\":4") && strstr((char*)after_, "\"moves\":5"));
    /* failing BEFORE the intent: FAILED, committed bytes untouched, retry works */
    size_t keep = n2; unsigned char snap[4096]; memcpy(snap, after_, n2);
    hook_target = "stage_write"; g.moves = 6;
    CHECK(fq_log_export(&o, &g, "export", &r) != 0 && r.state == FQ_LOG_FAILED);
    hook_target = NULL;
    CHECK(slurp(p, after_, sizeof(after_)) == keep && !memcmp(snap, after_, keep));
    CHECK(fq_log_export(&o, &g, "export", &r) == 0 && r.state == FQ_LOG_EXPORTED);
    nls_set_checkpoint_hook(NULL);
    CHECK(fq_log_shutdown(&o, true, &s) == 0 && s.state == FQ_LOG_EXPORTED);
    CHECK(fq_log_shutdown(&o, false, NULL) != 0);
}

/* Why: backend ownership is balanced: every acquisition in an export is released (no leaked handle). */
static void test_backend_balanced(void) {
    new_root();
    LinkLabNativeFsState before, after;
    FqLogOptions o = opts(); FqGame g; fq_init(&g); FqLogResult r;
    linklab_native_fs_state(&before);
    for (int i = 0; i < 3; i++) CHECK(fq_log_export(&o, &g, "export", &r) == 0);
    char bad_event[] = "BAD";
    CHECK(fq_log_export(&o, &g, bad_event, &r) != 0);
    linklab_native_fs_state(&after);
    CHECK(after.opens - before.opens == 3 && after.closes - before.closes == 3 && !after.held);
}

int main(void) {
    mkdir("build", 0775);
    test_solve_requires_two_real_folds();
    test_asleep_simulates_nothing();
    test_resume_counted_once_and_spurious_ignored();
    test_squeeze_and_reset_and_practice();
    test_invalid_inputs();
    test_event_queue_bounds();
    test_app_session_end_to_end();
    test_log_schema_and_append();
    test_log_rejects_bad_parameters();
    test_log_corruption_and_unwritable_retained();
    test_validator_boundaries();
    test_log_rotation_at_limit();
    test_log_interrupted_commit_recovers();
    test_backend_balanced();
    puts("PASS test_fq: 14 host tests (core, app, log; host simulation, not hardware)");
    return 0;
}
