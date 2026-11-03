#include "fq_platform.h"
#include <stdio.h>

#define FQ_BUILD "0.1.0"

int main(int argc, char** argv) {
    FqPlatformInfo info;
    if (!fq_platform_init(&info, argc, argv)) { fprintf(stderr, "FoldQuest: platform init failed\n"); return 1; }
    FqLogOptions log = {info.sd_root, FQ_BUILD, info.model, info.touch, info.apt_hooks};
    FqApp app;
    fq_app_init(&app, &log);
    uint64_t last = fq_platform_now_us();
    while (fq_platform_main_loop() && !app.quit) {
        FqLifecycleEvent ev;
        bool lifecycle = false;
        while (fq_platform_pop_event(&ev)) { fq_app_lifecycle(&app, ev); lifecycle = true; }
        uint64_t now = fq_platform_now_us();
        /* Time spent suspended is never counted: after a lifecycle event the clock baseline resets. */
        uint32_t dt = (lifecycle || now < last || now - last > 100000u) ? 0u : (uint32_t)(now - last);
        last = now;
        FqInput in;
        fq_platform_poll(&in);
        fq_app_frame(&app, &in, dt);
        fq_app_draw(&app);
    }
    int rc = fq_app_shutdown(&app);
#ifndef __3DS__
    printf("HOST SIM (not hardware): moves=%u solved=%d resumes=%u practice=%u resets=%u squeezes=%u rejected_asleep=%u status=%s\n",
           app.game.moves, app.game.solved, app.game.resumes, app.game.practice_folds, app.game.resets,
           app.game.squeezes, app.game.rejected_asleep, app.status);
#endif
    fq_platform_shutdown();
    return rc ? 2 : 0;
}
