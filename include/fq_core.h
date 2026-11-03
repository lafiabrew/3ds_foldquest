#ifndef FQ_CORE_H
#define FQ_CORE_H
/* FoldQuest core: one fixed original room, two "folds" (phase 0 / phase 1).
 * A REAL suspend/resume (lid sleep or HOME) flips the fold exactly once on resume.
 * Pure logic: no SDK, no I/O, no clock. Asleep => nothing simulates. */
#include <stdbool.h>
#include <stdint.h>

#define FQ_W 11
#define FQ_H 5
#define FQ_LEVEL_ID 1u /* the single built-in room; exported as the log "seed" */

typedef enum {
    FQ_OK = 0,
    FQ_BLOCKED = 1,   /* wall or currently-closed fold gate */
    FQ_SOLVED = 2,    /* move accepted/ignored: room already solved */
    FQ_ASLEEP = -1,   /* console asleep: input rejected, nothing simulated */
    FQ_BAD_ARG = -2,  /* NULL, out-of-range direction/coordinate, not adjacent */
    FQ_NOT_ASLEEP = -3 /* resume without suspend (spurious) */
} FqResult;

typedef enum { FQ_UP = 0, FQ_DOWN, FQ_LEFT, FQ_RIGHT, FQ_DIR_COUNT } FqDir;
typedef enum { FQ_CAUSE_LID_SLEEP = 0, FQ_CAUSE_HOME_SUSPEND = 1, FQ_CAUSE_COUNT } FqCause;

typedef struct {
    uint8_t x, y, phase;
    bool asleep, solved;
    FqCause cause; /* cause of the current/last suspend */
    unsigned moves, resumes, practice_folds, resets, squeezes; /* unsigned (not uint32_t): %u-safe on ARM */
    unsigned blocked, rejected_asleep, spurious_resumes, redundant_suspends;
    unsigned resumes_by_cause[FQ_CAUSE_COUNT];
    uint64_t awake_us; /* only accumulates while awake */
} FqGame;

void fq_init(FqGame*);
/* Tile char at (x,y) for the given phase: '#', '.', 'G', or ' ' out of range.
 * Gate 'a' is closed in phase 0, open in phase 1; gate 'b' is the opposite. */
char fq_tile(unsigned phase, int x, int y);
bool fq_is_open(const FqGame*, int x, int y);
FqResult fq_move(FqGame*, FqDir);
/* Bottom-screen grid cell tap; must be orthogonally adjacent to the player. */
FqResult fq_move_to(FqGame*, int x, int y);
FqResult fq_tick(FqGame*, uint32_t dt_us); /* FQ_ASLEEP => nothing advanced */
FqResult fq_suspend(FqGame*, FqCause);    /* idempotent; repeats are counted */
FqResult fq_resume(FqGame*);              /* flips fold once; squeeze check */
FqResult fq_practice_fold(FqGame*);       /* manual rehearsal; NOT a lifecycle event */
FqResult fq_reset(FqGame*);               /* room back to start; counters kept */
const char* fq_result_name(FqResult);
#endif
