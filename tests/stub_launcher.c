/* NEGATIVE CONTROL for scripts/verify_3ds.py: compiles and links for the same ARM target with
 * libctru, runs an APT main loop and prints text, but contains NO FoldQuest game code.
 * The verifier must reject it for the missing game/lifecycle/log call path, not for wrong machine. */
#include <3ds.h>
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); }
    gfxExit();
    return 0;
}
