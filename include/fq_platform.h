#ifndef FQ_PLATFORM_H
#define FQ_PLATFORM_H
#include "fq_app.h"

typedef struct {
    const char* model; /* "o3ds"/"n3ds" from a real query, NULL when not known */
    bool touch, apt_hooks;
    const char* sd_root;
} FqPlatformInfo;

bool fq_platform_init(FqPlatformInfo*, int argc, char** argv);
void fq_platform_shutdown(void);
bool fq_platform_main_loop(void);
void fq_platform_poll(FqInput*);
uint64_t fq_platform_now_us(void);
bool fq_platform_pop_event(FqLifecycleEvent*);
void fq_draw_begin(void);
void fq_draw_screen(int bottom); /* 0 top (400x240), 1 bottom (320x240) */
void fq_draw_end(void);
void fq_draw_rect(float x, float y, float w, float h, uint32_t rgba);
void fq_draw_text(float x, float y, float scale, uint32_t rgba, const char* text);
uint32_t fq_rgba(uint8_t r, uint8_t g, uint8_t b);
#endif
