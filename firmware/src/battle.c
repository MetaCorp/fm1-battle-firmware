/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors. Built on SLOOP (isod89) and Felucca (Leo Kuroshita, Hügelton
 * Instruments). FM-1 Battle is independent and not affiliated with M-VAVE. */
/* FM-1 Battle: the battle mode (battle.h says what SLOOP's files see of it). Included by editor.c with BATTLE_MODE,
 * after SLOOP's own commands: it uses the editor's frame helpers (ed_b, ed_pack7..), the project and storage code.
 *
 *   IDLE --ARM--> ARMED --SETUP+COMMIT--> READY --START--> RUNNING --PLAY held 2 s + PLAY / time up--> SUBMITTED
 *        --ACK--> DONE --a key / RESULT--> IDLE ; ABORT from anywhere --> IDLE
 *
 * ARMED: the player's song is in flash (flushed at ARM) and stays there untouched; the RAM is wiped, the panel shows
 * the waiting room and nothing sounds. SETUP puts the pack on the tracks (RAM only, fast: it runs in the countdown),
 * COMMIT checks its hash and writes the record. RUNNING: the clock runs, the PRESETS list is the pack, the guards
 * keep the locks. The battle sector (BT_SEC) holds the record and the battle's working project; the sections A..D
 * of the battle live in .noinit RAM (they survive a reset, not a power-off). At the end the player's song is read
 * back from flash, as at power-on. docs/BATTLE_PROTOCOL.md has the frames. */

#define BT_PROTO 1u                              /* the battle protocol version (BT_INFO, the INFO tag) */
#define BT_FW_NAME "FM-1 BATTLE"
#define BT_FW_VERSION "0.1.0-DEV"

enum { BT_IDLE, BT_ARMED, BT_READY, BT_RUNNING, BT_SUBMITTED, BT_DONE };   /* the states (BT_INFO, BT_STATE) */
#define BTF_TEMPO 2u                             /* the tempo is locked */
#define BTF_EDIT 4u                              /* the pack's sounds may be edited */
#define BTF_TIMEUP 8u                            /* the clock ran out: submitted by itself */
#define BTF_REFUSED 16u                          /* BT_ACK said refused */
#define BTF_RESULT 32u                           /* a result to show (BT_RESULT) */
#define BTF_SETUP 64u                            /* BT_SETUP applied: the pack is on the tracks */
#define BT_TOKEN_MAX 96u

/* the record: the first 256 bytes of the battle sector */
typedef struct {
    uint32_t magic, seq;
    uint8_t state, flags, ntoken, sync;          /* sync: the player's GLO > SYNC, given back at the end */
    uint8_t id[16];                              /* the battle (a UUID's bytes) */
    uint8_t dev[8];                              /* this FM-1's id for the site (random, made once) */
    uint32_t duration_ms, elapsed_ms;            /* elapsed_ms: when the record was written */
    uint16_t bpm;
    uint8_t kit, res_rank;
    uint8_t eng[3], pre[3], res_n, rsv0;
    int16_t res_score;                           /* the average x 100 */
    uint8_t rsv1[2];
    uint32_t setup_crc;
    uint8_t token[BT_TOKEN_MAX];
    uint8_t arr[36];                             /* the battle's song order (arr_config_t) */
    char res_win[16];
    uint8_t rsv2[40];
    uint32_t crc;                                /* CRC-32 of everything before it */
} bt_rec_t;
_Static_assert(sizeof(bt_rec_t) == 256u, "the battle record is 256 bytes");

#define BT_SEC 0x000FB000u                       /* the battle sector: the record, then the battle's project */
#define BT_MAGIC 0x314C5442u                     /* "BTL1" */
#define BT_HOLD_MS 2000u                         /* PLAY held this long: the submit screen */
#define BT_QUIT_MS 3000u                         /* REC + PLAY held this long on a waiting screen: leave the battle */
#define BT_SAVE_GAP 15000u                       /* the battle's own autosave: at most this often */
_Static_assert(sizeof(bt_rec_t) + sizeof(project_t) <= 0x1000u, "the record and a project fit the battle sector");
#if FELUCCA_ARRANGER
_Static_assert(sizeof(arr_config_t) == sizeof(((bt_rec_t *)0)->arr), "the song order fits the record");
#endif

enum { BT_CMD_INFO = 100, BT_CMD_ARM, BT_CMD_LOBBY, BT_CMD_SETUP, BT_CMD_COMMIT, BT_CMD_START, BT_CMD_STATE,
       BT_CMD_SUBMIT, BT_CMD_ACK, BT_CMD_RESULT, BT_CMD_ABORT, BT_CMD_LINK, BT_CMD_LAST = BT_CMD_LINK };
enum { BT_OK, BT_EARGS, BT_ESTATE, BT_ESTOP, BT_EFLASH, BT_EHASH, BT_EBATTLE };
#define BT_REFUSED 0x7Fu                         /* an editor command refused during a battle: cmd, 7F, 7F */

#ifndef BT_FL_ERASE                              /* (tests/battle_test.c: straight into the RAM image) */
UI_SMALL static int bt_fl_erase(uint32_t off) { uint32_t took; return fl_erase4k_quiet(off, &took); }
#define BT_FL_ERASE(off) bt_fl_erase(off)
#define BT_FL_WRITE(off, p, n) fl_write((off), (const uint8_t *)(p), (n))
#endif

static bt_rec_t bt;                              /* the battle as it is now (BT_IDLE: none) */
static struct {
    uint32_t t0;                                 /* fm1_ms at elapsed 0, while RUNNING */
    uint32_t push_ms, hold_t0, quit_t0, save_ms, check_ms, link_ms, cd_end, proj_sum, scr_sig;
    uint16_t lobby_bpm;
    uint8_t loaded, confirm, holding, quitting, lobby_n, lobby_ready, scr;
    uint8_t bank[3];                             /* the pack's sounds as BANK entries (ui.c): the PRESETS list */
    int8_t ptch_id;                              /* FM6's PTCH parameter (P_E0 + k), -1 none */
    char genre[13], title[21], link[17];
} btr;

UI_SMALL static uint32_t bt_elapsed(void)
{
    uint32_t e = bt.state == BT_RUNNING ? fm1_ms - btr.t0 : bt.elapsed_ms;
    return e > bt.duration_ms ? bt.duration_ms : e;
}
UI_SMALL static uint32_t bt_left(void) { return bt.duration_ms - bt_elapsed(); }

UI_SMALL static int battle_holds_flash(void) { return bt.state != BT_IDLE && bt.state != BT_DONE; }
UI_SMALL static int battle_locked(void) { return bt.state >= BT_READY && bt.state <= BT_SUBMITTED; }
UI_SMALL static int battle_list(void) { return battle_locked() && (bt.flags & BTF_SETUP); }
UI_SMALL static int battle_muted(void) { return bt.state == BT_ARMED || bt.state == BT_READY; }
UI_SMALL static int battle_engine_ok(uint32_t e)
{
    return !battle_list() || e == bt.eng[0] || e == bt.eng[1] || e == bt.eng[2];
}

/* ---- the PRESETS list during a battle: the pack's three sounds, on every synth track (ui.c preset_pos / _at) */
UI_SMALL static uint32_t battle_preset_pos(uint32_t *total)
{
    uint32_t i;
    *total = 3;
    for (i = 0; i < 3u; i++)
        if (BANK[btr.bank[i]].e == TSEL->eng_req && bank_pi[btr.bank[i]] == TSEL->preset)
            return i;
    return 0;
}
UI_SMALL static uint32_t battle_preset_at(uint32_t n, uint32_t *k) { return base_at(btr.bank[n % 3u], k); }

UI_SMALL static int bt_bank_of(uint32_t e, uint32_t p)    /* the BANK entry of engine e's factory preset p, -1 */
{
    uint32_t i;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (BANK[i].e == e && bank_pi[i] == p)
            return (int)i;
    return -1;
}
UI_SMALL static int bt_edit_id(uint32_t e, const char *label)  /* P_E0 + k of engine e's EDIT parameter, -1 */
{
    uint32_t k;
    const engine_t *en = ENGINES[e % NENGINES];
    for (k = 0; k < 8u; k++)
        if (en->edit[k].label && str_eq(en->edit[k].label, label))
            return (int)(P_E0 + k);
    return -1;
}
UI_SMALL static void bt_derive(void)                      /* what SETUP's values give (also after a reset) */
{
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        int b = bt_bank_of(bt.eng[k], bt.pre[k]);
        btr.bank[k] = (uint8_t)(b < 0 ? 0 : b);
    }
    btr.ptch_id = (int8_t)bt_edit_id(ENGI_FM6, "PTCH");
}

/* ---- the record and the battle's project, in the battle sector */
UI_SMALL static int bt_write(void)
{
    if (!flash_ok)
        return 0;                                /* (no flash: the battle runs in RAM until a reset) */
    bt.magic = BT_MAGIC;
    bt.seq++;
    bt.elapsed_ms = bt_elapsed();
#if FELUCCA_ARRANGER
    if (bt.flags & BTF_SETUP)
        memcpy(bt.arr, &arrangement, sizeof bt.arr);
#endif
    bt.crc = st_crc32(&bt, sizeof bt - 4u);
    if (BT_FL_ERASE(BT_SEC) || BT_FL_WRITE(BT_SEC, &bt, sizeof bt))
        return -1;
    btr.save_ms = fm1_ms;
    if ((bt.flags & BTF_SETUP) && battle_holds_flash()) {
        proj_capture(&autosave_buf);             /* (the autosave is held during a battle: its buffer is free) */
        btr.proj_sum = autosave_buf.sum;
        if (BT_FL_WRITE(BT_SEC + sizeof bt, &autosave_buf, sizeof autosave_buf))
            return -1;
    }
    return 0;
}
UI_SMALL static void battle_load_record(void)             /* persist_boot, once flash_ok is known */
{
    btr.loaded = 1;
    if (flash_ok && st_read(BT_SEC, &bt, sizeof bt) == 0 && bt.magic == BT_MAGIC &&
        bt.crc == st_crc32(&bt, sizeof bt - 4u) && bt.state <= BT_DONE)
        return;
    memset(&bt, 0, sizeof bt);
}

/* ---- the songs: wiped for the battle, the pack put on, the player's read back */
UI_SMALL static void bt_wipe(void)                        /* ARM: an empty song (the player's is in flash) */
{
    uint32_t s;
    project_new();
    fm1_irq_off();
    for (s = 0; s < 4u; s++)
        memset(&proj_slot[s], 0, sizeof proj_slot[s]);
    live_sec = -1;
    fm1_irq_on();
    sec_dirty = 0;
    song_dirty = 0;
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
}
UI_SMALL static void bt_apply_pack(void)                  /* SETUP: the pack on tracks 1..3, the kit on 4 */
{
    uint32_t k;
    project_new();
    for (k = 0; k < 3u; k++) {                   /* (as preset_go: the engine, then its preset) */
        set_engine_of(&trk[k], bt.eng[k]);
        apply_preset_to(&trk[k], bt.pre[k]);
    }
    TDRUM->p[P_E0] = (int16_t)bt.kit;
    song.g[G_BPM] = (int16_t)bt.bpm;
    if (bt.flags & BTF_TEMPO)
        song.g[G_SYNC] = 0;
    song.sel = 0;
    sync_reload = 1;
    ui.force = 1;
}
UI_SMALL static void bt_player_back(void)                 /* the player's song, from flash, as at power-on */
{
    uint32_t s;
    int n;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();
    for (s = 0; s < 4u; s++)
        memset(&proj_slot[s], 0, sizeof proj_slot[s]);
    live_sec = -1;
    fm1_irq_on();
#if FELUCCA_FLASH
    for (s = 0; s < 4u; s++)
        if (flash_ok)
            proj_fetch(s);
#endif
    sec_dirty = 0;
    song_dirty = 0;
    n = flash_ok ? st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp) : -1;
    if (n > 0 && proj_import(&autosave_buf, &proj_tmp, n)) {
        project_apply(&autosave_buf);
        autosave_hash = autosave_buf.sum;
    } else {
        project_new();
    }
#if FELUCCA_ARRANGER && FELUCCA_FLASH
    if (persist_saved.magic == PERSIST_MAGIC && arr_valid(&persist_saved.arrangement, 15u))
        arrangement = persist_saved.arrangement;
    else
        arr_defaults(&arrangement);
#endif
    lights_sync = bt.sync;                       /* GLO > SYNC as the player had it (the tempo lock forced INT) */
    song.g[G_SYNC] = (int16_t)bt.sync;
#if FELUCCA_FLASH
    {   /* the settings: written again only if they are not the player's any more (a menu change in the battle) */
        persist_t p;
        persist_fill(&p);
        settings_later = 0;
        song_dirty = flash_ok && memcmp(&p, &persist_saved, sizeof p) != 0;
    }
#endif
    sync_reload = 1;
    ui.force = 1;
}
UI_SMALL static void bt_end(uint32_t state)               /* ACK (DONE) or ABORT (IDLE) */
{
    bt.state = (uint8_t)state;                   /* first: the flash is the player's again */
    bt.flags &= (uint8_t)(BTF_REFUSED | BTF_RESULT | BTF_TIMEUP);
    btr.confirm = btr.holding = btr.quitting = 0;
    bt_player_back();
    (void)bt_write();
}
UI_SMALL static void battle_boot(void)                    /* after felucca_init: a battle that was on goes on */
{
    if (!btr.loaded)
        battle_load_record();
    if (!battle_holds_flash())
        return;
    if (bt.flags & BTF_SETUP) {
        bt_derive();
        if (st_read(BT_SEC + sizeof bt, &autosave_buf, sizeof autosave_buf) == 0 && proj_ok(&autosave_buf))
            project_apply(&autosave_buf);
        else
            bt_apply_pack();                     /* (the project was lost: the pack again) */
        btr.proj_sum = autosave_buf.sum;
#if FELUCCA_ARRANGER
        if (arr_valid((const arr_config_t *)bt.arr, 15u))
            memcpy(&arrangement, bt.arr, sizeof bt.arr);
        else
            arr_defaults(&arrangement);
#endif
    } else {
        bt_wipe();
    }
    if (bt.state == BT_RUNNING)
        btr.t0 = fm1_ms - bt.elapsed_ms;         /* (the time it was off is not counted: START resyncs) */
}

/* ---- the guards: what the locks forbid is put back (a message says why) */
UI_SMALL static int bt_user_src(const track_t *t)         /* the sound reads a user sample slot */
{
    const char *n = ENGINES[t->eng_req % NENGINES]->name;
    int id;
    if (str_eq(n, "SAMPLE"))
        return (id = bt_edit_id(t->eng_req, "SET")) >= 0 && t->p[id] >= (int16_t)SMP_NSETS;
    if (str_eq(n, "GRAIN"))
        return (id = bt_edit_id(t->eng_req, "SRC")) >= 0 && t->p[id] >= (int16_t)SMP_NSETS;
    if (str_eq(n, "SLICE"))
        return (id = bt_edit_id(t->eng_req, "SRC")) >= 0 && t->p[id] >= 1;
    return 0;
}
UI_SMALL static void bt_guard(void)
{
    const char *why = 0;
    uint32_t k;
    if (!battle_list())
        return;
    if ((bt.flags & BTF_TEMPO) && (song.g[G_BPM] != (int16_t)bt.bpm || song.g[G_SYNC] != 0)) {
        song.g[G_BPM] = (int16_t)bt.bpm;
        song.g[G_SYNC] = 0;
        why = "TEMPO LOCKED";
    }
    for (k = 0; k < 3u; k++) {
        track_t *t = &trk[k];
        uint32_t j;
        if (!battle_engine_ok(t->eng_req) || user_of(t) < UP_SLOTS || bt_user_src(t)) {
            set_engine_of(t, bt.eng[k]);
            apply_preset_to(t, bt.pre[k]);
            why = "PACK SOUNDS ONLY";
            continue;
        }
        if (t->eng_req != ENGI_FM6 || btr.ptch_id < 0 || t->preset >= ENGINES[ENGI_FM6]->npresets)
            continue;
        for (j = 0; j < 3u; j++) {               /* FM6: the patch stays the pack sound's (PTCH is another sound) */
            int16_t v = (int16_t)ENGINES[ENGI_FM6]->presets[t->preset].e[btr.ptch_id - P_E0];
            if (bt.eng[j] == ENGI_FM6 && bt.pre[j] == t->preset && t->p[btr.ptch_id] != v) {
                t->p[btr.ptch_id] = v;
                why = "FM6 PATCH LOCKED";
            }
        }
    }
    if (TDRUM->p[P_E0] != (int16_t)bt.kit) {
        TDRUM->p[P_E0] = (int16_t)bt.kit;
        why = "KIT LOCKED";
    }
    if (song.g[G_TXSRC] > 6) {                   /* GLO > TEXTURE: USR1..4 are samples */
        song.g[G_TXSRC] = 0;
        why = "NO SAMPLES";
    }
    if (why) {
        ui_message(why);
        sync_reload = 1;
    }
}

/* ---- submit */
UI_SMALL static void bt_submit(int timeup)
{
    if (bt.state != BT_RUNNING)
        return;
    bt.elapsed_ms = timeup ? bt.duration_ms : bt_elapsed();
    if (timeup)
        bt.flags |= BTF_TIMEUP;
    bt.state = BT_SUBMITTED;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    btr.confirm = btr.holding = 0;
    btr.push_ms = fm1_ms - 1000u;                /* (the push goes out at once) */
    (void)bt_write();
    ui.force = 1;
}
UI_SMALL static void battle_play_down(void)
{
    if (bt.state == BT_RUNNING && !btr.confirm) {
        btr.holding = 1;
        btr.hold_t0 = fm1_ms;
    }
}

/* ---- frames */
UI_SMALL static void bt_u35(uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) ed_b((v >> (7u * i)) & 127u); }
UI_SMALL static uint32_t bt_r35(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)(a[4] & 15u) << 28;
}
UI_SMALL static uint32_t bt_rstr(const uint8_t *a, uint32_t na, char *d, uint32_t max)   /* bytes taken (0 included) */
{
    uint32_t i = 0, n = 0;
    while (i < na && a[i]) {
        if (n + 1u < max)
            d[n++] = (char)(a[i] >= 32u ? a[i] : '?');
        i++;
    }
    d[n] = 0;
    return i < na ? i + 1u : i;
}
UI_SMALL static void bt_state_out(void)                   /* state, flags, elapsed, left */
{
    ed_b(bt.state);
    ed_b(bt.flags & 127u);
    bt_u35(bt_elapsed());
    bt_u35(bt_left());
}
UI_SMALL static void bt_push(uint32_t cmd)
{
    ed_begin(cmd);
    if (cmd == BT_CMD_SUBMIT) {
        ed_pack7(bt.id, sizeof bt.id);
        bt_u35(bt.elapsed_ms);
        ed_b(bt.flags & 127u);
    } else {
        bt_state_out();
    }
    ed_send();
}
UI_SMALL static void bt_dev_make(void)                    /* this FM-1's id: made once, kept in the record */
{
    uint32_t a, b, i;
    for (i = 0; i < sizeof bt.dev; i++)
        if (bt.dev[i])
            return;
    a = fm1_ticks() ^ fm1_ms * 2654435761u ^ (uint32_t)song.batt_raw;
    b = st_crc32(&a, 4) ^ fm1_ticks() * 40503u ^ ui_input_ms;
    memcpy(bt.dev, &a, 4);
    memcpy(bt.dev + 4, &b, 4);
    for (i = 0; i < sizeof bt.dev; i++)
        bt.dev[i] = (uint8_t)(bt.dev[i] | (i == 0));   /* (never all zero) */
    (void)bt_write();
}
UI_SMALL static void battle_info_tag(void)                /* INFO ends with it: this is FM-1 Battle */
{
    ed_str("BATTLE", 8);
    ed_b(BT_PROTO);
}

/* an editor command while a battle is on: the reads go through, the writes are refused (the beat is played on the
 * FM-1, not drawn on a computer) */
UI_SMALL static int bt_ed_allowed(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    switch (cmd) {
    case ED_INFO: case ED_GET: case ED_DUMP: case ED_DESC: case ED_STEP_GET: case ED_NAMES: case ED_SMP_INFO:
    case ED_UP_LIST: case ED_UP_GET: case ED_WATCH: case ED_PING: case ED_TRACK_DUMP: case ED_BK_LIST:
    case ED_BK_GET: case ED_LOCK_GET: case ED_MICRO_GET: case ED_FILL_GET: case ED_FM6_LIST: case 72u: case 73u:
        return 1;
    case ED_TRACK:
        return na == 0;
    case ED_TRACK_MIX: case ED_DRUM_STEP:
        return na == 1;
    case ED_TRACK_STEP: case ED_TRACK_PARAM:
        return na == 2;
    case ED_LIVE:
        return na >= 1 && a[0] == 0;
    }
    return 0;
}

UI_SMALL static void bt_cmd(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t rc = BT_OK, i;
    uint8_t id[16];
    switch (cmd) {
    case BT_CMD_INFO:                            /* -> proto, state, flags, id, dev, elapsed, left, duration s, bpm, names */
        bt_dev_make();
        ed_b(BT_PROTO);
        bt_state_out();
        ed_pack7(bt.id, sizeof bt.id);
        ed_pack7(bt.dev, sizeof bt.dev);
        bt_u35(bt.duration_ms / 1000u);
        ed_v(bt.bpm);
        ed_str(BT_FW_NAME " " BT_FW_VERSION, 24);
        ed_str(FELUCCA_VERSION, 24);              /* "SLOOP 2.8.5" */
        return;
    case BT_CMD_STATE:
        bt_state_out();
        return;
    case BT_CMD_ARM:                             /* id (pack7, 19), duration s (u35), flags, bpm (v14) */
        if (na < 27u || ed_unpack7(a, 19u, id, sizeof id) != sizeof id)
            rc = BT_EARGS;
        else if (bt.state == BT_ARMED && !memcmp(id, bt.id, sizeof id))
            rc = BT_OK;                          /* (asked again: it is armed) */
        else if (battle_holds_flash())
            rc = memcmp(id, bt.id, sizeof id) ? BT_EBATTLE : BT_ESTATE;
        else if (song.playing || (transport_req && transport_req != 2u))   /* (a stop on its way is fine) */
            rc = BT_ESTOP;
        else {
            uint32_t s = bt_r35(a + 19);
            persist_flush_now();                 /* what was only in RAM goes to flash: the player's song is there */
            if (flash_ok && sec_dirty) {
                rc = BT_EFLASH;
                break;
            }
            memcpy(bt.id, id, sizeof id);
            bt.sync = lights_sync;
            bt.state = BT_ARMED;
            bt.flags = (uint8_t)(a[24] & (BTF_TEMPO | BTF_EDIT));
            bt.duration_ms = (s < 60u ? 60u : s > 3600u ? 3600u : s) * 1000u;
            bt.elapsed_ms = 0;
            bt.bpm = (uint16_t)clamp(ed_rv(a + 25), GP[G_BPM].min, GP[G_BPM].max);
            bt.ntoken = 0;
            bt.res_rank = bt.res_n = 0;
            btr.lobby_n = btr.lobby_ready = 0;
            btr.cd_end = 0;
            btr.genre[0] = btr.title[0] = 0;
            bt_wipe();
            if (bt_write()) {
                bt_end(BT_IDLE);
                rc = BT_EFLASH;
            }
        }
        break;
    case BT_CMD_LOBBY:                           /* players, ready, countdown s (127 none), bpm (v14), genre, title */
        if (na < 5u) {
            rc = BT_EARGS;
            break;
        }
        if (bt.state != BT_ARMED && bt.state != BT_READY) {
            rc = BT_ESTATE;
            break;
        }
        btr.lobby_n = a[0];
        btr.lobby_ready = a[1];
        btr.cd_end = a[2] < 127u ? fm1_ms + a[2] * 1000u : 0u;
        btr.lobby_bpm = (uint16_t)clamp(ed_rv(a + 3), 0, 999);
        i = 5u + bt_rstr(a + 5, na - 5u, btr.genre, sizeof btr.genre);
        (void)bt_rstr(a + i, na > i ? na - i : 0u, btr.title, sizeof btr.title);
        break;
    case BT_CMD_SETUP: {                         /* bpm (v14), engine + preset x 3, kit -> hash (u35) */
        uint8_t raw[9];
        uint32_t k, bpm;
        if (na < 9u) {
            rc = BT_EARGS;
            break;
        }
        if (bt.state != BT_ARMED) {
            rc = BT_ESTATE;
            break;
        }
        bpm = (uint32_t)ed_rv(a);
        if (bpm < (uint32_t)GP[G_BPM].min || bpm > (uint32_t)GP[G_BPM].max || a[8] >= DRUM_USR)
            rc = BT_EARGS;
        for (k = 0; k < 3u && rc == BT_OK; k++)
            if (a[2 + 2 * k] >= NENGINES || bt_bank_of(a[2 + 2 * k], a[3 + 2 * k]) < 0)
                rc = BT_EARGS;
        if (rc != BT_OK)
            break;
        bt.bpm = (uint16_t)bpm;
        for (k = 0; k < 3u; k++) {
            bt.eng[k] = a[2 + 2 * k];
            bt.pre[k] = a[3 + 2 * k];
        }
        bt.kit = a[8];
        raw[0] = (uint8_t)bpm;                   /* the canonical bytes the server hashes too */
        raw[1] = (uint8_t)(bpm >> 8);
        for (k = 0; k < 7u; k++)
            raw[2 + k] = a[2 + k];
        bt.setup_crc = st_crc32(raw, sizeof raw);
        bt_derive();
        bt_apply_pack();
        bt.flags |= BTF_SETUP;
        ed_b(rc);
        ed_b(bt.state);
        bt_u35(bt.setup_crc);
        return;
    }
    case BT_CMD_COMMIT:                          /* hash (u35) */
        if (na < 5u)
            rc = BT_EARGS;
        else if (bt.state == BT_READY && bt_r35(a) == bt.setup_crc)
            rc = BT_OK;
        else if (bt.state != BT_ARMED || !(bt.flags & BTF_SETUP))
            rc = BT_ESTATE;
        else if (bt_r35(a) != bt.setup_crc)
            rc = BT_EHASH;
        else {
            bt.state = BT_READY;
            if (bt_write())
                rc = BT_EFLASH;
        }
        break;
    case BT_CMD_START: {                         /* offset ms (u35), token (pack7) */
        uint8_t tok[BT_TOKEN_MAX];
        uint32_t nt, off;
        if (na < 6u) {
            rc = BT_EARGS;
            break;
        }
        off = bt_r35(a);
        nt = ed_unpack7(a + 5, na - 5u, tok, sizeof tok);
        if (off > bt.duration_ms)
            off = bt.duration_ms;
        if (bt.state == BT_RUNNING) {            /* again (a reconnection): the server's clock wins */
            if (nt != bt.ntoken || memcmp(tok, bt.token, nt))
                rc = BT_EBATTLE;
            else
                btr.t0 = fm1_ms - off;
            break;
        }
        if (bt.state != BT_READY || !nt) {
            rc = bt.state != BT_READY ? BT_ESTATE : BT_EARGS;
            break;
        }
        memcpy(bt.token, tok, nt);
        bt.ntoken = (uint8_t)nt;
        bt.state = BT_RUNNING;
        btr.t0 = fm1_ms - off;
        btr.cd_end = 0;
        ui_message("GO!");
        if (bt_write())
            rc = BT_EFLASH;
        break;
    }
    case BT_CMD_SUBMIT:                          /* from the site (its Submit button, D9): as PLAY held + PLAY */
        if (bt.state == BT_RUNNING)
            bt_submit(0);
        else if (bt.state != BT_SUBMITTED)
            rc = BT_ESTATE;
        break;
    case BT_CMD_ACK:                             /* result: 0 received, 1 refused */
        if (na < 1u)
            rc = BT_EARGS;
        else if (bt.state != BT_SUBMITTED)
            rc = BT_ESTATE;
        else {
            if (a[0])
                bt.flags |= BTF_REFUSED;
            bt_end(BT_DONE);
        }
        break;
    case BT_CMD_RESULT:                          /* rank, players, score x 100 (v14), winner */
        if (na < 5u)
            rc = BT_EARGS;
        else if (battle_holds_flash())
            rc = BT_ESTATE;
        else {
            bt.res_rank = a[0];
            bt.res_n = a[1];
            bt.res_score = (int16_t)ed_rv(a + 2);
            (void)bt_rstr(a + 4, na - 4u, bt.res_win, sizeof bt.res_win);
            bt.flags |= BTF_RESULT;
            bt.state = BT_DONE;
            (void)bt_write();
        }
        break;
    case BT_CMD_ABORT:
        if (bt.state != BT_IDLE)
            bt_end(BT_IDLE);
        break;
    case BT_CMD_LINK:                            /* pseudo -> rc, dev */
        (void)bt_rstr(a, na, btr.link, sizeof btr.link);
        btr.link_ms = fm1_ms | 1u;
        bt_dev_make();
        ed_b(rc);
        ed_pack7(bt.dev, sizeof bt.dev);
        return;
    default:
        rc = BT_EARGS;
        break;
    }
    ed_b(rc);
    ed_b(bt.state);
}

UI_SMALL static int battle_editor(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    if (!btr.loaded)
        battle_load_record();
    if (cmd >= BT_CMD_INFO && cmd <= BT_CMD_LAST) {
        bt_cmd(cmd, a, na);
        return 1;
    }
    if (!battle_holds_flash() || bt_ed_allowed(cmd, a, na))
        return 0;
    ed_b(BT_REFUSED);
    ed_b(BT_REFUSED);
    return 1;
}

/* ---- main loop */
UI_SMALL static void battle_poll(void)
{
    uint32_t now = fm1_ms, play = 1u << panel.btn[B_PLAY], rec = 1u << panel.btn[B_REC];
    if (!btr.loaded)
        battle_load_record();
    if (bt.state == BT_IDLE)
        return;
    bt_guard();
    if (bt.state == BT_ARMED || bt.state == BT_READY || bt.state == BT_SUBMITTED) {   /* REC + PLAY held: leave */
        if ((fm1_in.buttons & (play | rec)) != (play | rec))
            btr.quitting = 0;
        else if (!btr.quitting) {
            btr.quitting = 1;
            btr.quit_t0 = now;
        } else if (now - btr.quit_t0 >= BT_QUIT_MS) {
            bt_end(BT_IDLE);
            ui_message("LEFT THE BATTLE");
            return;
        }
    }
    if (bt.state == BT_RUNNING) {
        if (btr.holding && !(fm1_in.buttons & play))
            btr.holding = 0;
        if (btr.holding && now - btr.hold_t0 >= BT_HOLD_MS) {
            btr.holding = 0;
            btr.confirm = 1;
            ui.force = 1;
        }
        if (now - btr.t0 >= bt.duration_ms) {
            bt_submit(1);
        } else if (!song.playing && !transport_req && now - ui_input_ms >= 2500u && now - btr.save_ms >= BT_SAVE_GAP &&
                   now - btr.check_ms >= 1000u) {
            btr.check_ms = now;                  /* the battle's autosave (the player's is held) */
            proj_capture(&autosave_buf);
            if (autosave_buf.sum != btr.proj_sum && audio_quiet())
                (void)bt_write();
        }
    }
    if (usb.config && (bt.state == BT_RUNNING || bt.state == BT_SUBMITTED) && now - btr.push_ms >= 1000u && ed_room()) {
        btr.push_ms = now;
        bt_push(BT_CMD_STATE);
        if (bt.state == BT_SUBMITTED && ed_room())
            bt_push(BT_CMD_SUBMIT);
    }
}
