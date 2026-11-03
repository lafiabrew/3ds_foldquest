/* Native 3DS platform: libctru HID/APT, citro2d drawing. Lifecycle comes from REAL APT hooks. */
#include "fq_platform.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

static C3D_RenderTarget *top, *bottom;
static C2D_TextBuf text_buf;
static bool gfx_up, c3d_up, c2d_up, hook_up;

static void unwind(void) {
    if (text_buf) { C2D_TextBufDelete(text_buf); text_buf = NULL; }
    if (c2d_up) { C2D_Fini(); c2d_up = false; }
    if (c3d_up) { C3D_Fini(); c3d_up = false; }
    if (hook_up) { fq_apt_lifecycle_shutdown(); hook_up = false; }
    if (gfx_up) { gfxExit(); gfx_up = false; }
    top = bottom = NULL;
}

bool fq_platform_init(FqPlatformInfo* info, int argc, char** argv) {
    (void)argc; (void)argv;
    gfxInitDefault(); gfx_up = true;
    gfxSet3D(false); /* 2D only: no stereoscopic dependency */
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { unwind(); return false; }
    c3d_up = true;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) { unwind(); return false; }
    c2d_up = true;
    C2D_Prepare();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    text_buf = C2D_TextBufNew(4096);
    if (!top || !bottom || !text_buf) { unwind(); return false; }
    if (fq_apt_lifecycle_init()) { unwind(); return false; }
    hook_up = true;
    bool is_new = false;
    info->model = R_SUCCEEDED(APT_CheckNew3DS(&is_new)) ? (is_new ? "n3ds" : "o3ds") : NULL;
    info->touch = true; info->apt_hooks = true; info->sd_root = "sdmc:";
    return true;
}
void fq_platform_shutdown(void) { unwind(); }
bool fq_platform_main_loop(void) { return aptMainLoop(); }
uint64_t fq_platform_now_us(void) { return svcGetSystemTick() * 1000000ull / SYSCLOCK_ARM11; }
bool fq_platform_pop_event(FqLifecycleEvent* e) { return fq_apt_lifecycle_pop(e); }

void fq_platform_poll(FqInput* in) {
    memset(in, 0, sizeof(*in));
    hidScanInput();
    u32 d = hidKeysDown();
    if (d & (KEY_DUP | KEY_CPAD_UP)) in->down |= FQ_IN_UP;
    if (d & (KEY_DDOWN | KEY_CPAD_DOWN)) in->down |= FQ_IN_DOWN;
    if (d & (KEY_DLEFT | KEY_CPAD_LEFT)) in->down |= FQ_IN_LEFT;
    if (d & (KEY_DRIGHT | KEY_CPAD_RIGHT)) in->down |= FQ_IN_RIGHT;
    if (d & KEY_Y) in->down |= FQ_IN_PRACTICE;
    if (d & KEY_SELECT) in->down |= FQ_IN_RESET;
    if (d & KEY_X) in->down |= FQ_IN_EXPORT;
    if (d & KEY_START) in->down |= FQ_IN_QUIT;
    if (d & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        in->touch_down = true; in->tx = tp.px; in->ty = tp.py;
    }
}

static float avail_w = 400.0f;
void fq_draw_begin(void) { C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(text_buf); }
void fq_draw_screen(int b) {
    C3D_RenderTarget* rt = b ? bottom : top;
    avail_w = b ? 320.0f : 400.0f;
    C2D_TargetClear(rt, 0);
    C2D_SceneBegin(rt);
}
void fq_draw_end(void) { C3D_FrameEnd(0); }
uint32_t fq_rgba(uint8_t r, uint8_t g, uint8_t b) { return C2D_Color32(r, g, b, 255); }
void fq_draw_rect(float x, float y, float w, float h, uint32_t c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); }
void fq_draw_text(float x, float y, float s, uint32_t c, const char* text) {
    C2D_Text t;
    if (!text || !*text) return;
    if (!C2D_TextParse(&t, text_buf, text)) return;
    C2D_TextOptimize(&t);
    float sx = s;
    if (t.width * sx > avail_w - x - 4.0f) sx = (avail_w - x - 4.0f) / t.width; /* fit, never clip */
    C2D_DrawText(&t, C2D_WithColor, x, y - s * 4.0f, 0.5f, sx, s, c);
}
