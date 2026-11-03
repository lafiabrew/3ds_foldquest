#include "fq_lifecycle.h"

bool fq_events_push(FqEventQueue* q, FqLifecycleEvent e) {
    if (!q || e == FQ_EV_NONE || (unsigned)e > FQ_EV_RESUME) return false;
    if (q->count == FQ_EVENT_CAP) { q->dropped++; return false; }
    q->items[(q->head + q->count) % FQ_EVENT_CAP] = e;
    q->count++;
    return true;
}
bool fq_events_pop(FqEventQueue* q, FqLifecycleEvent* out) {
    if (!q || !out || !q->count) return false;
    *out = q->items[q->head];
    q->head = (q->head + 1) % FQ_EVENT_CAP;
    q->count--;
    return true;
}
