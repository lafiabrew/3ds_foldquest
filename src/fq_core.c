#include "fq_core.h"
#include <string.h>

/* 'a' closed in phase 0 / open in phase 1; 'b' the opposite. One fold to pass 'a',
 * a second fold to pass 'b'. Row 3 is a harmless dead end. */
static const char* const level[FQ_H] = {
    "###########",
    "#@..a.b..G#",
    "#.#########",
    "#.........#",
    "###########"};

void fq_init(FqGame* g) {
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->x = 1; g->y = 1;
}

char fq_tile(unsigned phase, int x, int y) {
    if (x < 0 || y < 0 || x >= FQ_W || y >= FQ_H) return ' ';
    char c = level[y][x];
    if (c == '@') return '.';
    if (c == 'a') return phase ? '.' : '#';
    if (c == 'b') return phase ? '#' : '.';
    return c;
}
bool fq_is_open(const FqGame* g, int x, int y) {
    char c = g ? fq_tile(g->phase, x, y) : ' ';
    return c == '.' || c == 'G';
}

static void check_goal(FqGame* g) {
    if (fq_tile(g->phase, g->x, g->y) == 'G') g->solved = true;
}

FqResult fq_move(FqGame* g, FqDir d) {
    static const int dx[FQ_DIR_COUNT] = {0, 0, -1, 1}, dy[FQ_DIR_COUNT] = {-1, 1, 0, 0};
    if (!g || (unsigned)d >= FQ_DIR_COUNT) return FQ_BAD_ARG;
    if (g->asleep) { g->rejected_asleep++; return FQ_ASLEEP; }
    if (g->solved) return FQ_SOLVED;
    int nx = g->x + dx[d], ny = g->y + dy[d];
    if (!fq_is_open(g, nx, ny)) { g->blocked++; return FQ_BLOCKED; }
    g->x = (uint8_t)nx; g->y = (uint8_t)ny; g->moves++;
    check_goal(g);
    return FQ_OK;
}

FqResult fq_move_to(FqGame* g, int x, int y) {
    if (!g || x < 0 || y < 0 || x >= FQ_W || y >= FQ_H) return FQ_BAD_ARG;
    int ddx = x - g->x, ddy = y - g->y;
    if (ddx == 0 && ddy == 0) return FQ_BAD_ARG;
    if ((ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy) != 1) return FQ_BAD_ARG;
    return fq_move(g, ddy < 0 ? FQ_UP : ddy > 0 ? FQ_DOWN : ddx < 0 ? FQ_LEFT : FQ_RIGHT);
}

FqResult fq_tick(FqGame* g, uint32_t dt_us) {
    if (!g) return FQ_BAD_ARG;
    if (g->asleep) { return FQ_ASLEEP; }
    g->awake_us += dt_us;
    return FQ_OK;
}

static void flip(FqGame* g) {
    g->phase ^= 1u;
    if (!fq_is_open(g, g->x, g->y)) { /* a gate closed on the player: back to the start */
        g->x = 1; g->y = 1; g->squeezes++;
    }
}

FqResult fq_suspend(FqGame* g, FqCause cause) {
    if (!g || (unsigned)cause >= FQ_CAUSE_COUNT) return FQ_BAD_ARG;
    if (g->asleep) { g->redundant_suspends++; return FQ_ASLEEP; }
    g->asleep = true; g->cause = cause;
    return FQ_OK;
}

FqResult fq_resume(FqGame* g) {
    if (!g) return FQ_BAD_ARG;
    if (!g->asleep) { g->spurious_resumes++; return FQ_NOT_ASLEEP; }
    g->asleep = false;
    g->resumes++; g->resumes_by_cause[g->cause]++;
    if (!g->solved) flip(g); /* a solved room stays solved; the counter still records the resume */
    return FQ_OK;
}

FqResult fq_practice_fold(FqGame* g) {
    if (!g) return FQ_BAD_ARG;
    if (g->asleep) { g->rejected_asleep++; return FQ_ASLEEP; }
    if (g->solved) return FQ_SOLVED;
    g->practice_folds++;
    flip(g);
    return FQ_OK;
}

FqResult fq_reset(FqGame* g) {
    if (!g) return FQ_BAD_ARG;
    if (g->asleep) { g->rejected_asleep++; return FQ_ASLEEP; }
    g->x = 1; g->y = 1; g->phase = 0; g->solved = false; g->moves = 0;
    g->resets++;
    return FQ_OK;
}

const char* fq_result_name(FqResult r) {
    switch (r) {
    case FQ_OK: return "OK";
    case FQ_BLOCKED: return "BLOCKED";
    case FQ_SOLVED: return "SOLVED";
    case FQ_ASLEEP: return "ASLEEP";
    case FQ_BAD_ARG: return "BAD_ARG";
    case FQ_NOT_ASLEEP: return "NOT_ASLEEP";
    }
    return "UNKNOWN";
}
