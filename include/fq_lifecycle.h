#ifndef FQ_LIFECYCLE_H
#define FQ_LIFECYCLE_H
/* Lifecycle events produced by the platform (real APT hooks on 3DS) and consumed by the main loop. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { FQ_EV_NONE = 0, FQ_EV_SUSPEND_LID, FQ_EV_SUSPEND_HOME, FQ_EV_RESUME } FqLifecycleEvent;
#define FQ_EVENT_CAP 16u

typedef struct {
    FqLifecycleEvent items[FQ_EVENT_CAP];
    unsigned head, count, dropped;
} FqEventQueue;

/* Bounded; a full queue drops the NEW event and counts it (never overwrites, never blocks). */
bool fq_events_push(FqEventQueue*, FqLifecycleEvent);
bool fq_events_pop(FqEventQueue*, FqLifecycleEvent*);

/* Native APT hook adapter (src/apt_lifecycle.c). The hook only enqueues; it does no work. */
int fq_apt_lifecycle_init(void);   /* 0 ok */
void fq_apt_lifecycle_shutdown(void);
bool fq_apt_lifecycle_pop(FqLifecycleEvent*);
unsigned fq_apt_lifecycle_dropped(void);
#endif
