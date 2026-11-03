#ifndef FQ_APP_H
#define FQ_APP_H
/* FoldQuest session: input mapping, lifecycle handling, explicit diagnostic export, drawing. */
#include "fq_core.h"
#include "fq_lifecycle.h"
#include "fq_log.h"

enum { FQ_IN_UP = 1, FQ_IN_DOWN = 2, FQ_IN_LEFT = 4, FQ_IN_RIGHT = 8, FQ_IN_PRACTICE = 16,
       FQ_IN_RESET = 32, FQ_IN_EXPORT = 64, FQ_IN_QUIT = 128 };

typedef struct {
    uint32_t down;    /* newly pressed this frame */
    bool touch_down;  /* touch began this frame */
    int tx, ty;       /* bottom-screen pixels */
} FqInput;

/* Bottom-screen layout (320x240). */
#define FQ_GRID_X 28
#define FQ_GRID_Y 28
#define FQ_CELL 24
#define FQ_BTN_Y 176
#define FQ_BTN_H 48

typedef struct {
    FqGame game;
    FqLogOptions log;
    FqLogResult last_export;
    bool exported_any, quit;
    char status[96];
} FqApp;

void fq_app_init(FqApp*, const FqLogOptions*);
void fq_app_lifecycle(FqApp*, FqLifecycleEvent);
void fq_app_frame(FqApp*, const FqInput*, uint32_t dt_us);
/* Quit path: one bounded recovery if an export is pending, then release SD ownership. */
int fq_app_shutdown(FqApp*);
void fq_app_draw(const FqApp*);
#endif
