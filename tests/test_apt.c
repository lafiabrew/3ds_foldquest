/* Host test of the REAL src/apt_lifecycle.c hook adapter against tests/shim/3ds.h (a declaration
 * shim, not an SDK). Why: proves the hook->event mapping, that the hook does no work itself,
 * bounded queue use, and hook install/remove cleanup. Not evidence of device behavior. */
#include <3ds.h>
#include "fq_app.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

static aptHookCookie* installed;
static unsigned installs, removals;
void aptHook(aptHookCookie* c, aptHookFn fn, void* p) { c->callback = fn; c->param = p; installed = c; installs++; }
void aptUnhook(aptHookCookie* c) { CHECK(c == installed); installed = NULL; removals++; }

static void fire(APT_HookType t) { CHECK(installed); installed->callback(t, installed->param); }

int main(void) {
    FqLifecycleEvent e;
    CHECK(!fq_apt_lifecycle_pop(&e));
    fq_apt_lifecycle_shutdown();                        /* never-initialised shutdown is a no-op */
    CHECK(removals == 0);
    CHECK(fq_apt_lifecycle_init() == 0 && installs == 1);
    CHECK(fq_apt_lifecycle_init() != 0 && installs == 1); /* no double hook */
    /* Lid sleep and wake */
    fire(APTHOOK_ONSLEEP);
    CHECK(fq_apt_lifecycle_pop(&e) && e == FQ_EV_SUSPEND_LID);
    fire(APTHOOK_ONWAKEUP);
    CHECK(fq_apt_lifecycle_pop(&e) && e == FQ_EV_RESUME);
    /* HOME suspend and restore */
    fire(APTHOOK_ONSUSPEND); fire(APTHOOK_ONRESTORE);
    CHECK(fq_apt_lifecycle_pop(&e) && e == FQ_EV_SUSPEND_HOME);
    CHECK(fq_apt_lifecycle_pop(&e) && e == FQ_EV_RESUME);
    fire(APTHOOK_ONEXIT);                               /* exit produces no game event */
    CHECK(!fq_apt_lifecycle_pop(&e));
    /* Feeding the hook events into the real app: the room folds once per wake */
    FqApp app; fq_app_init(&app, NULL);
    fire(APTHOOK_ONSLEEP); fire(APTHOOK_ONWAKEUP);
    while (fq_apt_lifecycle_pop(&e)) fq_app_lifecycle(&app, e);
    CHECK(app.game.phase == 1 && app.game.resumes == 1 && app.game.resumes_by_cause[FQ_CAUSE_LID_SLEEP] == 1);
    /* Overflow is bounded and counted, never blocking */
    for (unsigned i = 0; i < FQ_EVENT_CAP + 3; i++) fire(APTHOOK_ONSLEEP);
    CHECK(fq_apt_lifecycle_dropped() == 3);
    fq_apt_lifecycle_shutdown();
    CHECK(removals == 1 && installed == NULL);
    CHECK(fq_apt_lifecycle_init() == 0 && !fq_apt_lifecycle_pop(&e)); /* fresh queue after re-init */
    fq_apt_lifecycle_shutdown();
    puts("PASS test_apt: real APT hook adapter mapping/cleanup against declaration shim");
    return 0;
}
