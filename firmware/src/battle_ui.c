/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors. Built on SLOOP (isod89) and Felucca (Leo Kuroshita, Hügelton
 * Instruments). FM-1 Battle is independent and not affiliated with M-VAVE. */
/* FM-1 Battle: the screens (after battle.c, included by editor.c). A battle screen takes the whole panel while it is
 * up (battle_input: the keys and knobs are its own, nothing sounds or records) and is drawn instead of the page
 * (battle_draw). While the battle runs, the pages are SLOOP's with the clock in the top bar (battle_head). Short
 * English, as SLOOP's own screens. */

enum { BS_NONE, BS_WAIT, BS_CONFIRM, BS_SENT, BS_DONE, BS_RESULT, BS_LINK };

UI_SMALL static uint32_t bt_screen(void)
{
    if (btr.link_ms && fm1_ms - btr.link_ms < 4000u && bt.state != BT_RUNNING)
        return BS_LINK;
    btr.link_ms = btr.link_ms && fm1_ms - btr.link_ms < 4000u ? btr.link_ms : 0u;
    switch (bt.state) {
    case BT_ARMED:
    case BT_READY:
        return BS_WAIT;
    case BT_RUNNING:
        return btr.confirm ? BS_CONFIRM : BS_NONE;
    case BT_SUBMITTED:
        return BS_SENT;
    case BT_DONE:
        return (bt.flags & BTF_RESULT) ? BS_RESULT : BS_DONE;
    }
    return BS_NONE;
}

UI_SMALL static void bt_mmss(char *b, uint32_t ms)          /* "9:59" */
{
    uint32_t s = (ms + 999u) / 1000u, m = s / 60u;
    s %= 60u;
    fmt_int(b, (int32_t)m);
    b += str_len(b);
    *b++ = ':';
    *b++ = (char)('0' + s / 10u);
    *b++ = (char)('0' + s % 10u);
    *b = 0;
}

UI_SMALL static int battle_input(uint32_t pressed, uint32_t notes)
{
    uint32_t scr = bt_screen(), k;
    if (scr == BS_NONE)
        return 0;
    for (k = 0; k < NE; k++)
        (void)panel_enc(k);                      /* (the knobs wait: no jump when the page is back) */
    if (scr == BS_CONFIRM) {
        if (pressed & (1u << panel.btn[B_PLAY])) {
            bt_submit(0);
        } else if (pressed || notes) {
            btr.confirm = 0;
            ui_message("SUBMIT CANCELLED");
            ui.force = 1;
        }
    } else if ((scr == BS_DONE || scr == BS_RESULT) && (pressed || notes)) {
        bt.flags &= (uint8_t)~(BTF_RESULT | BTF_REFUSED | BTF_TIMEUP);
        bt.state = BT_IDLE;
        (void)bt_write();
        ui.force = 1;
    } else if (scr == BS_LINK && (pressed || notes)) {
        btr.link_ms = 0;
        ui.force = 1;
    }
    return 1;
}

UI_SMALL static void bt_line(uint32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    draw_text_box(0, y, 240, f, s, c, 1);
}

/* the lobby and the countdown are drawn whole, in two strips of 120 rows (cv_oy: the same absolute coordinates in
 * both), so a redraw never flickers */
UI_SMALL static void bt_c(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)   /* centred */
{
    cv_text(120 - text_w(f, s) / 2, y, f, s, c);
}

UI_SMALL static void bt_fit(char *d, const char *s, uint32_t n, int32_t w)   /* s cut to w px, with ".." */
{
    uint32_t k;
    str_cpy(d, s, n);
    if (text_w(&FONT_S, d) <= w)
        return;
    for (k = str_len(d); k > 0u; k--) {
        d[k - 1u] = 0;
        if (k + 1u < n && text_w(&FONT_S, d) + text_w(&FONT_S, "..") <= w) {
            str_cpy(d + str_len(d), "..", n - str_len(d));
            return;
        }
    }
}

/* the waiting room's band: the battle's tempo as the TRACKS header shows it, its name and length on the right */
UI_SMALL static void bt_band(void)
{
    char b[16];
    uint32_t m = bt.duration_ms / 60000u, s = bt.duration_ms / 1000u % 60u;
    fmt_int(b, bt.bpm);
    cv_text(4, 4, &FONT_L, b, TE_G4);
    cv_text(4 + text_w(&FONT_L, b) + 4, 20, &FONT_S, "bpm", TE_G3);
    cv_text(236 - text_w(&FONT_S, "fm-1 battle"), 5, &FONT_S, "fm-1 battle", TE_COL[3]);
    if (s) {
        bt_mmss(b, bt.duration_ms);
    } else {
        fmt_int(b, (int32_t)m);
        str_cpy(b + str_len(b), " min", sizeof b - str_len(b));
    }
    cv_text(236 - text_w(&FONT_S, b), 22, &FONT_S, b, TE_G3);
    cv_rect(0, 39, 240, 1, TE_G1);
}

/* the players, one tile each (8 at most), the ready ones lit */
UI_SMALL static void bt_tiles(int32_t y)
{
    uint32_t n = btr.lobby_n > 8u ? 8u : btr.lobby_n, r = btr.lobby_ready > n ? n : btr.lobby_ready, i;
    int32_t x = 120 - ((int32_t)n * 26 - 6) / 2;
    for (i = 0; i < n; i++, x += 26) {
        cv_rect(x, y, 20, 24, i < r ? TE_COL[1] : TE_G2);
        if (i >= r)
            cv_rect(x + 2, y + 2, 16, 20, C_BLACK);
    }
}

UI_SMALL static void bt_foot(void)
{
    bt_c(222, &FONT_S, btr.quitting ? "hold to leave..." : "rec+play held: leave", btr.quitting ? TE_RED : TE_G3);
}

UI_SMALL static uint32_t bt_cd_left(void)       /* ms left in the countdown, 0 none or over */
{
    return btr.cd_end && (int32_t)(btr.cd_end - fm1_ms) > 0 ? btr.cd_end - fm1_ms : 0u;
}

/* armed (press READY on the site), then ready (waiting for the others) */
UI_SMALL static void bt_lobby(void)
{
    char b[28];
    uint32_t ready = bt.state == BT_READY, more = btr.lobby_n > btr.lobby_ready ? btr.lobby_n - btr.lobby_ready : 0u;
    bt_band();
    bt_fit(b, btr.title[0] ? btr.title : "battle lobby", sizeof b, 232);
    bt_c(54, &FONT_S, b, C_WHITE);
    bt_c(72, &FONT_S, btr.genre[0] ? btr.genre : "any style", TE_G4);
    if (ready) {
        bt_c(100, &FONT_L, "READY", TE_COL[1]);
        if (!btr.lobby_n || !more) {
            str_cpy(b, btr.lobby_n ? "everyone is ready" : "waiting for the others", sizeof b);
        } else {
            str_cpy(b, "waiting for ", sizeof b);
            fmt_int(b + str_len(b), (int32_t)more);
            str_cpy(b + str_len(b), more == 1u ? " player" : " players", sizeof b - str_len(b));
        }
        bt_c(136, &FONT_S, b, TE_G4);
    } else {
        bt_c(100, &FONT_L, "PRESS READY", C_WHITE);
        bt_c(136, &FONT_S, "on the site", TE_G4);
    }
    if (btr.lobby_n) {
        bt_tiles(162);
        fmt_int(b, btr.lobby_ready);
        str_cpy(b + str_len(b), " / ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), btr.lobby_n);
        str_cpy(b + str_len(b), " ready", sizeof b - str_len(b));
        bt_c(192, &FONT_S, b, TE_G3);
    } else {
        bt_c(178, &FONT_S, "silent until the start", TE_G3);
    }
    bt_foot();
}

/* the countdown: the seconds in big digits, a bar that empties, red for the last three */
UI_SMALL static void bt_countdown(void)
{
    char b[28];
    uint32_t ms = bt_cd_left(), sec = (ms + 999u) / 1000u, len = btr.cd_s ? btr.cd_s * 1000u : 15000u, nd, i, w;
    uint16_t c = sec <= 3u ? TE_RED : C_WHITE;
    int32_t x;
    bt_band();
    bt_c(52, &FONT_S, sec ? "starts in" : "starting...", TE_G4);
    if (sec > 999u) sec = 999u;
    nd = sec >= 100u ? 3u : sec >= 10u ? 2u : 1u;
    x = 120 - ((int32_t)nd * 56 - 12) / 2;
    for (i = nd; i-- > 0u; x += 56) {
        uint32_t d = i == 2u ? sec / 100u : i == 1u ? sec / 10u % 10u : sec % 10u;
        te_digit(x, 76, 44, 76, 9, d, c);
    }
    w = ms >= len ? 200u : ms * 200u / len;
    cv_rect(20, 166, 200, 6, TE_G1);
    cv_rect(20, 166, (int32_t)w, 6, sec <= 3u ? TE_RED : TE_COL[1]);
    if (bt.state != BT_READY) {
        bt_c(186, &FONT_S, "press ready on the site", TE_RED);
    } else {
        bt_fit(b, btr.title[0] ? btr.title : "battle", sizeof b, 232);
        bt_c(184, &FONT_S, b, TE_G4);
        fmt_int(b, btr.lobby_n);
        str_cpy(b + str_len(b), btr.lobby_n == 1u ? " player" : " players", sizeof b - str_len(b));
        bt_c(200, &FONT_S, b, TE_G3);
    }
    bt_foot();
}

UI_SMALL static void bt_paint(void (*fn)(void))
{
    int32_t oy = cv_oy;
    uint32_t y;
    for (y = 0; y < 240u; y += 120u) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)y;
        fn();
        cv_oy = oy;
        cv_blit(0, y);
    }
}

UI_SMALL static int battle_draw(void)
{
    uint32_t scr = bt_screen(), sig, left = bt_left();
    char b[32];
    if (scr == BS_NONE) {
        if (btr.scr) {                           /* back to the page */
            btr.scr = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
        return 0;
    }
    sig = scr * 1000003u + bt.state * 7919u + btr.lobby_n * 131u + btr.lobby_ready * 17u + usb.config * 3u +
          (btr.cd_end ? (bt_cd_left() + 124u) / 125u * 65537u + 1u : 0u) + str_hash(5u, btr.title) +
          (left + 999u) / 1000u * 97u + btr.quitting * 1777u;
    if (scr == btr.scr && sig == btr.scr_sig && !ui.force)
        return 1;
    if (scr != btr.scr)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    btr.scr = (uint8_t)scr;
    btr.scr_sig = sig;
    ui.force = 0;
    switch (scr) {
    case BS_WAIT:
        bt_paint(btr.cd_end ? bt_countdown : bt_lobby);
        break;
    case BS_CONFIRM:
        bt_line(40, &FONT_S, "SUBMIT YOUR BEAT?", C_WHITE);
        bt_mmss(b, left);
        bt_line(80, &FONT_L, b, left < 60000u ? TE_RED : C_HI);
        bt_line(140, &FONT_S, "PLAY: SUBMIT", TE_COL[1]);
        bt_line(166, &FONT_S, "OTHER KEY: BACK", C_GRAY);
        bt_line(210, &FONT_S, "ONLY THE SEQUENCER COUNTS", C_DIM);
        break;
    case BS_SENT:
        bt_line(40, &FONT_S, (bt.flags & BTF_TIMEUP) ? "TIME IS UP" : "BEAT SUBMITTED", C_WHITE);
        bt_line(80, &FONT_L, "LOCKED", TE_COL[2]);
        bt_line(140, &FONT_S, usb.config ? "SENDING TO THE SITE" : "PLUG THE USB CABLE", usb.config ? C_HI : TE_RED);
        str_cpy(b, "TIME ", sizeof b);
        bt_mmss(b + 5, bt.elapsed_ms);
        bt_line(170, &FONT_S, b, C_GRAY);
        bt_line(214, &FONT_S, btr.quitting ? "HOLD TO LEAVE..." : "REC+PLAY HELD: LEAVE", C_DIM);
        break;
    case BS_DONE:
        bt_line(70, &FONT_L, (bt.flags & BTF_REFUSED) ? "REFUSED" : "SENT", (bt.flags & BTF_REFUSED) ? TE_RED : TE_COL[1]);
        bt_line(130, &FONT_S, "YOUR SONG IS BACK", C_HI);
        bt_line(156, &FONT_S, "LISTEN AND VOTE ON THE SITE", C_GRAY);
        bt_line(214, &FONT_S, "ANY KEY", C_DIM);
        break;
    case BS_RESULT:
        bt_line(20, &FONT_S, "RESULT", TE_COL[3]);
        b[0] = '#';
        fmt_int(b + 1, bt.res_rank);
        str_cpy(b + str_len(b), " / ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), bt.res_n);
        bt_line(56, &FONT_L, bt.res_rank ? b : "NOT RANKED", C_WHITE);
        str_cpy(b, "SCORE ", sizeof b);
        fmt_fix(b + 6, bt.res_score, 2);
        bt_line(110, &FONT_S, b, C_HI);
        bt_line(146, &FONT_S, "WINNER", C_GRAY);
        bt_line(168, &FONT_S, bt.res_win[0] ? bt.res_win : "-", TE_COL[2]);
        bt_line(214, &FONT_S, "ANY KEY", C_DIM);
        break;
    case BS_LINK: {
        static const char HEX[] = "0123456789ABCDEF";
        uint32_t i;
        bt_line(60, &FONT_S, "LINKED TO", C_GRAY);
        bt_line(90, &FONT_S, btr.link[0] ? btr.link : "?", C_WHITE);
        str_cpy(b, "FM-1 ", sizeof b);
        for (i = 0; i < 4u; i++) {
            b[5 + 2 * i] = HEX[bt.dev[i] >> 4];
            b[6 + 2 * i] = HEX[bt.dev[i] & 15u];
        }
        b[13] = 0;
        bt_line(140, &FONT_S, b, C_HI);
        break;
    }
    }
    return 1;
}

/* the top bar while the battle runs: the time left, red in the last minute */
UI_SMALL static uint32_t battle_head_sig(void)
{
    return battle_locked() ? bt.state * 31u + (bt_left() + 999u) / 1000u * 7u + 1u : 0u;
}
UI_SMALL static int32_t battle_head(int32_t x)
{
    char b[12];
    uint32_t left = bt_left();
    bt_mmss(b, left);
    return cv_text(x + 12, 1, &FONT_S, b, left < 60000u ? TE_RED : TE_COL[2]);
}
UI_SMALL static int32_t battle_head_r(int32_t xr, int32_t y)   /* the live screens' header (ui_studio.c te_header) */
{
    char b[12];
    uint32_t left = bt_left();
    int32_t x;
    bt_mmss(b, left);
    x = xr - text_w(&FONT_S, b);
    cv_text(x, y, &FONT_S, b, left < 60000u ? TE_RED : TE_COL[2]);
    return x;
}
