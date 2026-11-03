#include "fq_app.h"
#include "fq_platform.h"
#include <stdio.h>
#include <string.h>

static void say(FqApp* a, const char* msg) { snprintf(a->status, sizeof(a->status), "%s", msg); }

void fq_app_init(FqApp* a, const FqLogOptions* log) {
    memset(a, 0, sizeof(*a));
    fq_init(&a->game);
    if (log) a->log = *log;
    say(a, "Walk to G. Close the lid (or HOME) to FOLD the room.");
}

void fq_app_lifecycle(FqApp* a, FqLifecycleEvent e) {
    FqResult r;
    switch (e) {
    case FQ_EV_SUSPEND_LID: r = fq_suspend(&a->game, FQ_CAUSE_LID_SLEEP); break;
    case FQ_EV_SUSPEND_HOME: r = fq_suspend(&a->game, FQ_CAUSE_HOME_SUSPEND); break;
    case FQ_EV_RESUME:
        r = fq_resume(&a->game);
        if (r == FQ_OK) say(a, a->game.solved ? "Resumed. Room already solved." : "Resumed: the room FOLDED.");
        else say(a, "Resume without suspend ignored.");
        return;
    default: return;
    }
    if (r == FQ_OK) say(a, "Sleeping. Nothing runs until resume.");
}

static void export_log(FqApp* a) {
    FqLogResult r;
    fq_log_export(&a->log, &a->game, "export", &r);
    a->last_export = r;
    a->exported_any = true;
    say(a, fq_log_status_text(&r));
}

static void after_move(FqApp* a, FqResult r) {
    if (r == FQ_OK && a->game.solved) say(a, "SOLVED! SELECT resets the room.");
    else if (r == FQ_BLOCKED) say(a, "Blocked: wall or closed gate.");
    else if (r == FQ_ASLEEP) say(a, "Asleep: input ignored.");
    else if (r == FQ_BAD_ARG) say(a, "Tap a tile next to you.");
}

static void do_reset(FqApp* a) {
    FqResult r = fq_reset(&a->game);
    if (r == FQ_OK) say(a, "Room reset. Resume count kept.");
    else if (r == FQ_ASLEEP) say(a, "Asleep: input ignored.");
}
static void do_practice(FqApp* a) {
    FqResult r = fq_practice_fold(&a->game);
    if (r == FQ_OK) say(a, "Practice fold: not a real resume.");
    else if (r == FQ_SOLVED) say(a, "Solved: nothing to fold.");
}

void fq_app_frame(FqApp* a, const FqInput* in, uint32_t dt_us) {
    if (!a || !in) return;
    fq_tick(&a->game, dt_us);
    if (in->down & FQ_IN_QUIT) { a->quit = true; return; }
    static const struct { uint32_t bit; FqDir dir; } dirs[] = {
        {FQ_IN_UP, FQ_UP}, {FQ_IN_DOWN, FQ_DOWN}, {FQ_IN_LEFT, FQ_LEFT}, {FQ_IN_RIGHT, FQ_RIGHT}};
    for (unsigned i = 0; i < 4; i++)
        if (in->down & dirs[i].bit) { after_move(a, fq_move(&a->game, dirs[i].dir)); break; }
    if (in->down & FQ_IN_PRACTICE) do_practice(a);
    if (in->down & FQ_IN_RESET) do_reset(a);
    if (in->down & FQ_IN_EXPORT) export_log(a);
    if (in->touch_down) {
        int tx = in->tx, ty = in->ty;
        if (tx >= FQ_GRID_X && ty >= FQ_GRID_Y && tx < FQ_GRID_X + FQ_W * FQ_CELL && ty < FQ_GRID_Y + FQ_H * FQ_CELL) {
            after_move(a, fq_move_to(&a->game, (tx - FQ_GRID_X) / FQ_CELL, (ty - FQ_GRID_Y) / FQ_CELL));
        } else if (ty >= FQ_BTN_Y && ty < FQ_BTN_Y + FQ_BTN_H) {
            if (tx >= 8 && tx < 108) do_practice(a);
            else if (tx >= 116 && tx < 204) do_reset(a);
            else if (tx >= 212 && tx < 312) export_log(a);
        }
    }
}

int fq_app_shutdown(FqApp* a) {
    FqLogResult r;
    bool pending = a->exported_any && (a->last_export.state == FQ_LOG_PENDING);
    int rc = fq_log_shutdown(&a->log, pending, &r);
    a->last_export = r;
    return rc;
}

void fq_app_draw(const FqApp* a) {
    const FqGame* g = &a->game;
    char line[128];
    fq_draw_begin();
    fq_draw_screen(0);
    fq_draw_rect(0, 0, 400, 240, fq_rgba(18, 20, 34));
    fq_draw_text(12, 14, 0.9f, fq_rgba(255, 200, 90), "FOLDQUEST");
    snprintf(line, sizeof(line), "FOLD %c   moves %u   real folds %u   practice %u",
             g->phase ? 'B' : 'A', g->moves, g->resumes, g->practice_folds);
    fq_draw_text(12, 50, 0.6f, fq_rgba(220, 230, 255), line);
    snprintf(line, sizeof(line), "lid folds %u  HOME folds %u  squeezed %u  resets %u",
             g->resumes_by_cause[FQ_CAUSE_LID_SLEEP], g->resumes_by_cause[FQ_CAUSE_HOME_SUSPEND],
             g->squeezes, g->resets);
    fq_draw_text(12, 72, 0.5f, fq_rgba(160, 180, 220), line);
    fq_draw_text(12, 104, 0.55f, g->solved ? fq_rgba(120, 255, 150) : fq_rgba(255, 255, 255), a->status);
    fq_draw_text(12, 150, 0.45f, fq_rgba(160, 180, 220), "D-pad/Circle Pad or touch: move   Y: practice fold");
    fq_draw_text(12, 168, 0.45f, fq_rgba(160, 180, 220), "SELECT: reset room   X: export diagnostics   START: quit");
    fq_draw_text(12, 186, 0.45f, fq_rgba(160, 180, 220), "Close the lid or press HOME, then resume: the room folds.");
    fq_draw_text(12, 204, 0.4f, fq_rgba(120, 130, 160), "Runs on Old 3DS; no New 3DS-only features. State is RAM only.");
    fq_draw_screen(1);
    fq_draw_rect(0, 0, 320, 240, fq_rgba(10, 12, 22));
    for (int y = 0; y < FQ_H; y++)
        for (int x = 0; x < FQ_W; x++) {
            char t = fq_tile(g->phase, x, y);
            char raw = fq_tile(g->phase ^ 1u, x, y);
            uint32_t col = t == '#' ? fq_rgba(70, 76, 110) : t == 'G' ? fq_rgba(70, 200, 110) : fq_rgba(30, 34, 58);
            if (t == '.' && raw == '#') col = fq_rgba(90, 160, 210); /* gate open in this fold */
            if (t == '#' && raw == '.') col = fq_rgba(200, 120, 70); /* gate closed in this fold */
            fq_draw_rect((float)(FQ_GRID_X + x * FQ_CELL), (float)(FQ_GRID_Y + y * FQ_CELL), FQ_CELL - 2, FQ_CELL - 2, col);
        }
    fq_draw_rect((float)(FQ_GRID_X + g->x * FQ_CELL + 4), (float)(FQ_GRID_Y + g->y * FQ_CELL + 4), FQ_CELL - 10, FQ_CELL - 10,
                 fq_rgba(255, 230, 90));
    fq_draw_rect(8, FQ_BTN_Y, 100, FQ_BTN_H, fq_rgba(50, 60, 100));
    fq_draw_text(14, FQ_BTN_Y + 18, 0.5f, fq_rgba(255, 255, 255), "PRACTICE");
    fq_draw_rect(116, FQ_BTN_Y, 88, FQ_BTN_H, fq_rgba(100, 60, 60));
    fq_draw_text(128, FQ_BTN_Y + 18, 0.5f, fq_rgba(255, 255, 255), "RESET");
    fq_draw_rect(212, FQ_BTN_Y, 100, FQ_BTN_H, fq_rgba(50, 100, 70));
    fq_draw_text(222, FQ_BTN_Y + 18, 0.5f, fq_rgba(255, 255, 255), "EXPORT");
    fq_draw_end();
}
