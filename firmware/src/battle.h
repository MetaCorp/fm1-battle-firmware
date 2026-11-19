/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors. Built on SLOOP (isod89) and Felucca (Leo Kuroshita, Hügelton
 * Instruments). FM-1 Battle is independent and not affiliated with M-VAVE. */
/* FM-1 Battle: what SLOOP's own files see of the battle mode. The hooks in ui.c, ui_input.c, ui_draw.c, project.c,
 * editor.c and main.c call these functions; each hook says BATTLE and is listed in docs/UPSTREAM.md.
 *
 * With BATTLE_MODE defined (felucca.c, tests/battle_test.c) they are battle.c's and battle_ui.c's, included by
 * editor.c. Without it (every SLOOP test) they are the stubs below: the hooks do nothing and SLOOP is SLOOP.
 *
 * The player's song is never written while a battle is on: the autosave, the sections and the settings stay in flash
 * as they were (the project.c hooks hold their writes), the battle plays in RAM, and the battle sector keeps the
 * battle's record and working project, so a battle survives a reset. When it ends, the player's song is read back from
 * flash: nothing to restore, so nothing can be lost. The protocol: docs/BATTLE_PROTOCOL.md. */
#ifndef BATTLE_H
#define BATTLE_H

#ifdef BATTLE_MODE
static int battle_holds_flash(void);      /* a battle is on: the player's song is not written, nor read (project.c) */
static void battle_load_record(void);     /* persist_boot: the record, before the sections are filled */
static void battle_boot(void);            /* after felucca_init: the battle's project back, if one is on */
static void battle_poll(void);            /* main loop: the clock, the pushes, the guards, the battle's autosave */
static int battle_muted(void);            /* the waiting room: no sound */
static int battle_list(void);             /* the PRESETS list holds the pack only */
static int battle_locked(void);           /* a battle is on: the locks apply */
static int battle_engine_ok(uint32_t e);  /* engine e is one of the pack's (or no battle) */
static uint32_t battle_preset_pos(uint32_t *total);
static uint32_t battle_preset_at(uint32_t n, uint32_t *k);
static int battle_input(uint32_t pressed, uint32_t notes);   /* 1: a battle screen took the panel this pass */
static int battle_draw(void);             /* 1: a battle screen is drawn instead of the page */
static uint32_t battle_head_sig(void);
static int32_t battle_head(int32_t x);    /* the battle clock in the top bar from x; the next x */
static void battle_play_down(void);       /* PLAY pressed (ui_input.c) */
static int battle_editor(uint32_t cmd, const uint8_t *a, uint32_t na);   /* editor.c: 1 = handled, reply built */
static void battle_info_tag(void);        /* editor.c INFO: the battle tag */
#else
#define battle_holds_flash() 0
#define battle_load_record() ((void)0)
#define battle_boot() ((void)0)
#define battle_poll() ((void)0)
#define battle_muted() 0
#define battle_list() 0
#define battle_locked() 0
#define battle_engine_ok(e) ((void)(e), 1)
#define battle_preset_pos(t) ((void)(t), 0u)
#define battle_preset_at(n, k) ((void)(n), (void)(k), 0u)
#define battle_input(p, n) ((void)(p), (void)(n), 0)
#define battle_draw() 0
#define battle_head_sig() 0u
#define battle_head(x) (x)
#define battle_play_down() ((void)0)
#define battle_editor(c, a, n) ((void)(c), (void)(a), (void)(n), 0)
#define battle_info_tag() ((void)0)
#endif

#endif
