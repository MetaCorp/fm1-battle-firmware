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
          (btr.cd_end ? (btr.cd_end - fm1_ms + 999u) / 1000u * 65537u : 0u) + str_hash(5u, btr.title) +
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
        bt_line(10, &FONT_S, "FM-1 BATTLE", TE_COL[3]);
        bt_line(40, &FONT_S, btr.title[0] ? btr.title : "BATTLE LOBBY", C_WHITE);
        str_cpy(b, btr.genre[0] ? btr.genre : "ANY STYLE", sizeof b);
        str_cpy(b + str_len(b), "  ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), bt.bpm);
        str_cpy(b + str_len(b), " BPM", sizeof b - str_len(b));
        bt_line(62, &FONT_S, b, C_GRAY);
        if (btr.cd_end && (int32_t)(btr.cd_end - fm1_ms) > 0) {
            bt_mmss(b, btr.cd_end - fm1_ms);
            bt_line(96, &FONT_L, b, C_WHITE);
        } else {
            bt_line(96, &FONT_L, bt.state == BT_READY ? "READY" : "WAIT", bt.state == BT_READY ? TE_COL[1] : C_HI);
        }
        str_cpy(b, "PLAYERS ", sizeof b);
        fmt_int(b + str_len(b), btr.lobby_n);
        str_cpy(b + str_len(b), "  READY ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), btr.lobby_ready);
        bt_line(146, &FONT_S, b, C_AMB);
        bt_line(176, &FONT_S, bt.state == BT_READY ? "SOUNDS ON START" : "PRESS READY ON THE SITE", C_GRAY);
        bt_line(214, &FONT_S, btr.quitting ? "HOLD TO LEAVE..." : "REC+PLAY HELD: LEAVE", C_DIM);
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
