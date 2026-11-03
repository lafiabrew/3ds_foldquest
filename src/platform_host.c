/* HOST SIMULATION platform: scripted input and lifecycle events, recorded draw calls, fixed
 * 16667 us frame clock. It is NOT 3DS hardware, NOT APT, and proves nothing about a console. */
#include "fq_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 512
static char script[4096];
static char* tokens[MAX_TOKENS];
static unsigned ntok, next_tok;
static uint64_t clock_us;
static FqEventQueue events;
static FqInput pending;
static char drawn[8192];
static unsigned rects;

bool fq_platform_init(FqPlatformInfo* info, int argc, char** argv) {
    static const char* root;
    root = "build/test_sd";
    ntok = next_tok = 0; clock_us = 0; events = (FqEventQueue){0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sd-root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            if (snprintf(script, sizeof(script), "%s", argv[++i]) >= (int)sizeof(script)) return false;
        } else { fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]); return false; }
    }
    for (char* t = strtok(script, " "); t; t = strtok(NULL, " ")) {
        if (ntok == MAX_TOKENS) { fprintf(stderr, "script too long\n"); return false; }
        tokens[ntok++] = t;
    }
    info->model = NULL;  /* a host has no 3DS model */
    info->touch = false; info->apt_hooks = false; info->sd_root = root;
    return true;
}
void fq_platform_shutdown(void) {}
uint64_t fq_platform_now_us(void) { return clock_us; }

/* One script token per frame. Returns false when the script is exhausted. */
bool fq_platform_main_loop(void) {
    memset(&pending, 0, sizeof(pending));
    if (next_tok >= ntok) return false;
    const char* t = tokens[next_tok++];
    clock_us += 16667;
    if (!strcmp(t, "up")) pending.down = FQ_IN_UP;
    else if (!strcmp(t, "down")) pending.down = FQ_IN_DOWN;
    else if (!strcmp(t, "left")) pending.down = FQ_IN_LEFT;
    else if (!strcmp(t, "right")) pending.down = FQ_IN_RIGHT;
    else if (!strcmp(t, "practice")) pending.down = FQ_IN_PRACTICE;
    else if (!strcmp(t, "reset")) pending.down = FQ_IN_RESET;
    else if (!strcmp(t, "export")) pending.down = FQ_IN_EXPORT;
    else if (!strcmp(t, "quit")) pending.down = FQ_IN_QUIT;
    else if (!strcmp(t, "sleep")) fq_events_push(&events, FQ_EV_SUSPEND_LID);
    else if (!strcmp(t, "home")) fq_events_push(&events, FQ_EV_SUSPEND_HOME);
    else if (!strcmp(t, "wake")) fq_events_push(&events, FQ_EV_RESUME);
    else if (!strncmp(t, "tap:", 4)) {
        int x, y;
        if (sscanf(t + 4, "%d,%d", &x, &y) == 2) { pending.touch_down = true; pending.tx = x; pending.ty = y; }
    } /* "wait" or anything else: an idle frame */
    return true;
}
void fq_platform_poll(FqInput* in) { *in = pending; }
bool fq_platform_pop_event(FqLifecycleEvent* e) { return fq_events_pop(&events, e); }

void fq_draw_begin(void) { drawn[0] = 0; rects = 0; }
void fq_draw_screen(int bottom) { (void)bottom; }
void fq_draw_end(void) {}
void fq_draw_rect(float x, float y, float w, float h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; rects++; }
void fq_draw_text(float x, float y, float s, uint32_t c, const char* text) {
    (void)x; (void)y; (void)s; (void)c;
    size_t used = strlen(drawn);
    if (text && used + strlen(text) + 2 < sizeof(drawn)) { strcat(drawn, text); strcat(drawn, "\n"); }
}
uint32_t fq_rgba(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | 255u; }

/* Test inspection of the last drawn frame. */
const char* fq_host_drawn_text(void) { return drawn; }
unsigned fq_host_drawn_rects(void) { return rects; }
