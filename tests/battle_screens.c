/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors */
/* FM-1 Battle: every screen of a battle on the FM-1, drawn by the real UI (tests/editor_host.c: ui_draw.c with
 * battle_ui.c, on a framebuffer), to PPM for review: the lobby (waiting, ready, the countdown), the production (the
 * pages with the clock in the top bar, the last minute in red), the submit screen, submitted, sent back, the result,
 * the site's link.
 *   battle_screens [DIR]          (DIR/battle-*.ppm; default build/host) */
#define BATTLE_MODE 1
#include "editor_host.c"

static const char *outdir;
static void ppm(const char *name)
{
    char path[512];
    FILE *f;
    unsigned i;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t p = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void shot(const char *name)
{
    ui.force = 1;
    ui_frame();
    ui_frame();
    ppm(name);
}

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    harness_init();
    btr.loaded = 1;
    usb.config = 1;
    go_home();

    /* the lobby: armed, then ready, then the countdown */
    bt.state = BT_ARMED;
    bt.bpm = 92;
    bt.duration_ms = 600000u;
    str_cpy(btr.title, "Boom bap classique", sizeof btr.title);
    str_cpy(btr.genre, "HIP HOP", sizeof btr.genre);
    btr.lobby_n = 3;
    btr.lobby_ready = 1;
    shot("battle-1-lobby");
    bt.state = BT_READY;
    btr.lobby_ready = 3;
    shot("battle-2-ready");
    btr.cd_s = 15;                               /* the 15 s countdown, 12 s left, then the last 3 */
    btr.cd_end = fm1_ms + 11500u;
    shot("battle-3-countdown");
    btr.cd_end = fm1_ms + 2400u;
    shot("battle-3b-countdown-end");
    btr.cd_end = 0;

    /* the production: SLOOP's pages, the clock in the top bar */
    bt.state = BT_RUNNING;
    btr.t0 = fm1_ms - 152000u;                   /* 7:28 left */
    go_home();
    shot("battle-4-running-home");
    open_family(FAM_ENV);
    shot("battle-5-running-page");
    btr.t0 = fm1_ms - 552000u;                   /* 0:48 left: red */
    go_home();
    shot("battle-6-last-minute");
    btr.confirm = 1;                             /* PLAY held 2 s */
    shot("battle-7-submit");
    btr.confirm = 0;

    /* submitted, sent back, the result */
    bt.elapsed_ms = 552000u;
    bt.state = BT_SUBMITTED;
    shot("battle-8-submitted");
    bt.state = BT_DONE;
    shot("battle-9-sent");
    bt.flags |= BTF_RESULT;
    bt.res_rank = 1;
    bt.res_n = 3;
    bt.res_score = 450;
    str_cpy(bt.res_win, "MIRA", sizeof bt.res_win);
    shot("battle-10-result");
    bt.state = BT_IDLE;
    bt.flags = 0;

    /* the site linked to this FM-1 */
    str_cpy(btr.link, "leopold", sizeof btr.link);
    for (i = 0; i < 8u; i++) bt.dev[i] = (uint8_t)(0x3Au + i * 17u);
    btr.link_ms = fm1_ms;
    shot("battle-11-link");
    printf("battle screens in %s\n", outdir);
    return 0;
}
