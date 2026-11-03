/* Real libctru APT hook adapter. Compiled for ARM (3DS) and, against tests/shim/3ds.h, on the host
 * to test the hook->event mapping. aptMainLoop() invokes the hook on the main thread around lid
 * sleep (ONSLEEP/ONWAKEUP) and HOME suspend (ONSUSPEND/ONRESTORE). The hook only enqueues. */
#include "fq_lifecycle.h"
#include <3ds.h>

static aptHookCookie cookie;
static FqEventQueue queue;
static bool hooked;

static void hook(APT_HookType type, void* param) {
    (void)param;
    switch (type) {
    case APTHOOK_ONSLEEP: fq_events_push(&queue, FQ_EV_SUSPEND_LID); break;
    case APTHOOK_ONSUSPEND: fq_events_push(&queue, FQ_EV_SUSPEND_HOME); break;
    case APTHOOK_ONWAKEUP:
    case APTHOOK_ONRESTORE: fq_events_push(&queue, FQ_EV_RESUME); break;
    default: break; /* ONEXIT: the main loop ends via aptMainLoop() == false */
    }
}
int fq_apt_lifecycle_init(void) {
    if (hooked) return -1;
    queue = (FqEventQueue){0};
    aptHook(&cookie, hook, 0);
    hooked = true;
    return 0;
}
void fq_apt_lifecycle_shutdown(void) {
    if (!hooked) return;
    aptUnhook(&cookie);
    hooked = false;
}
bool fq_apt_lifecycle_pop(FqLifecycleEvent* e) { return fq_events_pop(&queue, e); }
unsigned fq_apt_lifecycle_dropped(void) { return queue.dropped; }
